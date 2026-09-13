#include <spdlog/spdlog.h>

#include "helpers.hpp"

void pin_thread(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (result) spdlog::error("Failed to pin thread to core {}", core_id);
    else spdlog::info("Pinned thread to core {}", core_id);
}

void set_priority(int priority) {
    struct sched_param param;
    param.sched_priority = 80;

    pthread_t current_thread = pthread_self();
    int result = pthread_setschedparam(current_thread, SCHED_FIFO, &param);
    if (result) spdlog::error("Failed to set RT priority (try running in sudo)");
    else spdlog::info("Set RT priority {}", priority);
}

double interpolate(int val) {
    return pow((clamp(val, 190, 840) - 190) / 650.0, 2.0f);
}

int16_t read_val(int serial_port) {
    uint8_t upper, lower;
    read(serial_port, &upper, 1);
    read(serial_port, &lower, 1);
    return (upper << 7) | lower;
}

uint64_t get_micros() {
    auto duration = chrono::steady_clock::now().time_since_epoch();
    uint64_t micros = chrono::duration_cast<chrono::microseconds>(duration).count();
    return micros;
}

void command() {
    int result = system("sudo ip link set can0 down && sudo ip link set can0 up");
    if (result) spdlog::error("Couldn't reset can bus.");
}

void reset_can() {
    jthread bg_thread(command);
    bg_thread.detach();
}

void log_status(StatusCode status) {
    if (status.IsError()) {
        spdlog::error("{}: {}", status.GetName(), status.GetDescription());
    }
}

void optmize_can_util(hardware::TalonFX *motor) {
    log_status(motor->OptimizeBusUtilization(0_Hz));
    log_status(motor->GetDeviceTemp().SetUpdateFrequency(UPDATE_FREQ));
    log_status(motor->GetSupplyCurrent().SetUpdateFrequency(UPDATE_FREQ));
}
