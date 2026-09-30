#ifndef __PLUGIN_INTERFACE_HPP
#define __PLUGIN_INTERFACE_HPP

namespace spdlog { class logger; }
namespace ctre::phoenix6 {
    class CANBus;
    namespace hardware { class TalonFX; }
}

struct PluginContext {
    spdlog::logger *logger;

    ctre::phoenix6::CANBus *bus;
    ctre::phoenix6::hardware::TalonFX *master;
    ctre::phoenix6::hardware::TalonFX *follower;
};

class Plugin {
protected:
    spdlog::logger *logger;
    const PluginContext *context;

    virtual void initialize() {}

public:
    Plugin(const PluginContext *context) {
        logger = context->logger;
        this->context = context;
        initialize();
    }
    virtual void run() = 0;
    virtual void stop() {}
    virtual ~Plugin() = default;
};

#define PUBLIC_SYMBOL __attribute__((visibility("default")))
#define ADD_PLUGIN_SYMBOLS(class_name)                              \
extern "C" {                                                        \
    PUBLIC_SYMBOL Plugin *create(const PluginContext *context) {    \
        return new class_name(context);                             \
    }                                                               \
    PUBLIC_SYMBOL void destroy(Plugin *plugin) {                    \
        delete plugin;                                              \
    }                                                               \
}

typedef Plugin *(*CreatePluginFn)(const PluginContext *);
typedef void (*DestroyPluginFn)(Plugin *);

#endif