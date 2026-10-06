#include <csignal>
#include <dlfcn.h>
#include <filesystem>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <ctre/phoenix/cci/Diagnostics_CCI.h>
#include <ctre/phoenix6/SignalLogger.hpp>
#include <ctre/phoenix6/TalonFX.hpp>
#include <ctre/phoenix6/controls/Follower.hpp>
#include <ctre/phoenix6/unmanaged/Unmanaged.hpp>

#include <spdlog/logger.h>
#include <spdlog/sinks/systemd_sink.h>

#include "plugin/interface.hpp"

#define WATCHER_TIMEOUT_MS 100

namespace fs = std::filesystem;
using namespace ctre::phoenix;
using namespace ctre::phoenix6;

int pin_thread(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    return result;
}

int set_priority(int priority) {
    sched_param param;
    param.sched_priority = priority;

    pthread_t current_thread = pthread_self();
    int result = pthread_setschedparam(current_thread, SCHED_FIFO, &param);
    return result;
}

uint64_t get_micros() {
    auto duration = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    return micros;
}

struct PluginSettings {
    const char *name;
    const char *path;
    int pin_core = -1;
    int priority = -1;
    int loop_ms = 0;
};

class PluginManager {
private:
    bool first_load = true;
    std::atomic<bool> loaded{false};

    const char *name;
    fs::path tmp_path;
    const PluginSettings *settings;

    spdlog::logger *logger;
    const PluginContext *context;

    void *dl_handle;

    DestroyPluginFn destroy;
    Plugin *plugin;

    std::jthread *thread;

    void loop(std::stop_token stoken) {
        if (!plugin)
            return;

        auto next_wakeup = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(settings->loop_ms);

        while (!stoken.stop_requested()) {
            plugin->run();

            next_wakeup += interval;
            std::this_thread::sleep_until(next_wakeup);
        }
    }

    void run(std::stop_token stoken) {
        logger->info("Starting \"{}\" plugin", name);

        if (settings->pin_core > -1) {
            int res = pin_thread(settings->pin_core);
            if (res)
                logger->warn("Failed to pin \"{}\" plugin to core {}: {}", name, settings->pin_core, strerror(res));
        }
        if (settings->priority > -1) {
            int res = set_priority(settings->priority);
            if (res)
                logger->warn("Failed set priority for \"{}\" plugin to {}: {}", name, settings->priority, strerror(res));
        }

        if (settings->loop_ms > 0)
            loop(stoken);
        else
            plugin->run();
    }

    void unload() {
        if (!loaded.load())
            return;

        logger->info("Unloading \"{}\" plugin", name);

        if (settings->loop_ms <= 0)
            plugin->stop();
        delete thread;

        destroy(plugin);
        dlclose(dl_handle);

        loaded.store(false);
    }

public:
    void reload() {
        if (first_load) {
            logger->info("Loading \"{}\" plugin", name);
            first_load = false;
        } else {
            logger->info("Reloading \"{}\" plugin", name);
            unload();
        }

        try {
            fs::copy_file(settings->path, tmp_path, fs::copy_options::overwrite_existing);
        } catch (const fs::filesystem_error &e) {
            logger->error("Error copying {} to tmp location: {}", settings->path, e.what());
            return;
        }
 
        dl_handle = dlopen(tmp_path.c_str(), RTLD_LAZY);
        if (!dl_handle) {
            logger->error("Error loading \"{}\" plugin:\n{}", name, dlerror());
            return;
        }
        dlerror();

        CreatePluginFn create = (CreatePluginFn)dlsym(dl_handle, "create");
        destroy = (DestroyPluginFn)dlsym(dl_handle, "destroy");
        const char *err = dlerror();
        if (err) {
            logger->error("Error finding create/destroy symbols in \"{}\" plugin", name);
            dlclose(dl_handle);
            return;
        }

        plugin = create(context);
        thread = new std::jthread(&PluginManager::run, this);

        loaded.store(true);
    }

    PluginManager(const PluginSettings *settings, const PluginContext *context) {
        name = settings->name;
        this->settings = settings;

        logger = context->logger;
        this->context = context;

        fs::path plugin_path(settings->path);
        tmp_path = fs::temp_directory_path() / plugin_path.filename();
        reload();
    }

    ~PluginManager() {
        unload();

        if (fs::exists(tmp_path) && fs::is_regular_file(tmp_path))
            fs::remove(tmp_path);
    }
};

spdlog::logger *logger;

PluginSettings plugin_settings[] = {
    {
        .name = "read",
        .path = "./plugin/libread.so",
        .pin_core = 2,
        .priority = 80,
    },
    {
        .name = "control",
        .path = "./plugin/libcontrol.so",
        .pin_core = 3,
        .priority = 75,
        .loop_ms = 5,
    },
    {
        .name = "telemetry",
        .path = "./plugin/libtelemetry.so",
    },
};
constexpr size_t NUM_PLUGINS = std::size(plugin_settings);

int fd, wd[NUM_PLUGINS];
PluginManager *plugin_managers[NUM_PLUGINS]{};

void watcher_thread(std::stop_token stoken) {
    if (fd < 0)
        return;
    for (int i = 0; i < NUM_PLUGINS; i++) {
        if (wd[i] < 0)
            return;
    }

    constexpr size_t EVENT_SIZE = sizeof(inotify_event);
    constexpr size_t BUF_LEN = 32 * (EVENT_SIZE + 16);
    char buffer[BUF_LEN];

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!stoken.stop_requested()) {
        pfd.revents = 0;

        int res = poll(&pfd, 1, WATCHER_TIMEOUT_MS);
        if (res == 0)
            continue;
        if (res < 0) {
            logger->error("Error while filewatching");
            break;
        }
        if (pfd.revents & POLLIN) {
            int length = read(fd, buffer, sizeof(buffer));
            int i = 0;
            while (i < length) {
                inotify_event *event = (inotify_event *)&buffer[i];
                for (int i = 0; i < NUM_PLUGINS; i++) {
                    if (wd[i] == event->wd)
                        plugin_managers[i]->reload();
                }
                i += EVENT_SIZE + event->len;
            }
        }
    }
}

int main() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);

    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    std::shared_ptr<spdlog::logger> shared_logger = spdlog::systemd_logger_mt("journal");
    logger = shared_logger.get();

    c_Phoenix_Diagnostics_SetSecondsToStart(-1);
    unmanaged::LoadPhoenix();
    SignalLogger::EnableAutoLogging(false);

    sleep(5);

    CANBus can_bus{"can0"};

    hardware::TalonFX master{0, can_bus};
    hardware::TalonFX follower{1, can_bus};

    controls::Follower follow_request{0, false};
    follower.SetControl(follow_request);

    sleep(1);

    std::atomic<bool> safe_to_run{false};
    std::atomic<double> throttle{0.0};
    std::atomic<double> target_tps{0.0};

    PluginContext context = {
        .logger = logger,

        .bus = &can_bus,
        .master = &master,
        .follower = &follower,

        .safe_to_run = &safe_to_run,
        .throttle = &throttle,
        .target_tps = &target_tps,
    };

    fd = inotify_init();
    if (fd < 0)
        logger->error("Error initializing inotify");
    for (int i = 0; i < NUM_PLUGINS; i++) {
        plugin_managers[i] = new PluginManager(&(plugin_settings[i]), &context);
        if (fd >= 0) {
            wd[i] = inotify_add_watch(fd, plugin_settings[i].path, IN_CLOSE_WRITE);
            if (wd[i] < 0)
                logger->error("Error adding inotify watch");
        }
    }

    std::jthread watcher(watcher_thread);

    int sig = 0;
    sigwait(&sigset, &sig);

    watcher.request_stop();
    if (watcher.joinable())
        watcher.join();

    for (int i = 0; i < NUM_PLUGINS; i++) {
        delete plugin_managers[i];
        if (fd >= 0 && wd[i] >= 0)
            inotify_rm_watch(fd, wd[i]);
    }
    if (fd >= 0)
        close(fd);

    logger->info("Finished cleanup");
}
