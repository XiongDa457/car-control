#include <ctre/phoenix6/TalonFX.hpp>
#include <spdlog/logger.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "plugin/interface.hpp"

using namespace ctre::phoenix6;

#define UPDATE_FREQ 50_Hz

#define GEARBOX_RATIO 0.224
#define GEARBOX_SPROCKET 15
#define WHEEL_SPROCKET 45
#define WHEEL_DIAMETER 0.508

void optmize_can_util(hardware::TalonFX *motor) {
    motor->ResetSignalFrequencies();
    motor->OptimizeBusUtilization(0_Hz);
    motor->GetVelocity().SetUpdateFrequency(UPDATE_FREQ);
    motor->GetDeviceTemp().SetUpdateFrequency(UPDATE_FREQ);
    motor->GetSupplyVoltage().SetUpdateFrequency(UPDATE_FREQ);
    motor->GetSupplyCurrent().SetUpdateFrequency(UPDATE_FREQ);
}

double tps_to_wheel_speed(double tps) {
    return tps * GEARBOX_RATIO * GEARBOX_SPROCKET / WHEEL_SPROCKET * WHEEL_DIAMETER * M_PI * 3.6;
}

class TelemetryPlugin : public Plugin {
private:
    httplib::Server svr;

    void initialize() override {
        optmize_can_util(context->master);
        optmize_can_util(context->follower);
    }

public:
    using Plugin::Plugin;

    void run() override {
        svr.Get("/get", [this](const httplib::Request& req, httplib::Response& res) {
            double master_tps = context->master->GetVelocity().GetValueAsDouble();
            double follower_tps = context->follower->GetVelocity().GetValueAsDouble();

            nlohmann::json j = {
                {"safeToRun", context->safe_to_run->load()},
                {"throttle", context->throttle->load()},
                {"target_tps", context->target_tps->load()},

                {"motor1", {
                    {"temp", context->master->GetDeviceTemp().GetValueAsDouble()},
                    {"voltage", context->master->GetSupplyVoltage().GetValueAsDouble()},
                    {"current", context->master->GetSupplyCurrent().GetValueAsDouble()},
                    {"tps", master_tps},
                    {"wheel_speed", tps_to_wheel_speed(master_tps)},
                }},
                {"motor2", {
                    {"temp", context->follower->GetDeviceTemp().GetValueAsDouble()},
                    {"voltage", context->follower->GetSupplyVoltage().GetValueAsDouble()},
                    {"current", context->follower->GetSupplyCurrent().GetValueAsDouble()},
                    {"tps", follower_tps},
                    {"wheel_speed", tps_to_wheel_speed(follower_tps)},
                }},
            };
            res.set_content(j.dump(), "application/json");
        });
        logger->info("Telemetry server listening on port 8080");
        svr.listen("0.0.0.0", 8080);
    }

    void stop() override {
        logger->info("Stopping telemetry server");
        svr.stop();
    }
};

ADD_PLUGIN_SYMBOLS(TelemetryPlugin)