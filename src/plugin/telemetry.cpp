#include <ctre/phoenix6/TalonFX.hpp>
#include <spdlog/logger.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "plugin/interface.hpp"

using namespace ctre::phoenix6;

#define UPDATE_FREQ 20_Hz

void optmize_can_util(hardware::TalonFX *motor) {
    motor->ResetSignalFrequencies();
    motor->OptimizeBusUtilization(0_Hz);
    motor->GetVelocity().SetUpdateFrequency(UPDATE_FREQ);
    motor->GetDeviceTemp().SetUpdateFrequency(UPDATE_FREQ);
    motor->GetSupplyVoltage().SetUpdateFrequency(UPDATE_FREQ);
    motor->GetSupplyCurrent().SetUpdateFrequency(UPDATE_FREQ);
}

class TelemetryPlugin : public Plugin {
private:
    httplib::Server svr;

    void initialize() override {
        optmize_can_util(context->master);
        optmize_can_util(context->follower);
    }

    void get(const httplib::Request& req, httplib::Response& res) {
    }

public:
    using Plugin::Plugin;

    void run() override {
        svr.Get("/get", [this](const httplib::Request& req, httplib::Response& res) {
            nlohmann::json j = {
                {"safeToRun", context->safe_to_run->load()},
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