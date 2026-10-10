#include <ctre/phoenix6/TalonFX.hpp>
#include <spdlog/logger.h>

#include <App.h>
#include <glaze/json.hpp>

#include "plugin/interface.hpp"

using namespace ctre::phoenix6;

#define CAN_UPDATE_FREQ 50_Hz

#define GEARBOX_RATIO 0.224
#define GEARBOX_SPROCKET 15
#define WHEEL_SPROCKET 45
#define WHEEL_DIAMETER 0.508

#define SERVER_PORT 8080
#define SERVER_SEND_MS 20

struct MotorData {
    double temp;
    double voltage;
    double current;
    double tps;
    double wheelSpeed;
};

struct JsonData {
    bool safeToRun;
    double throttle;
    double targetTps;
    MotorData motor1;
    MotorData motor2;
};

struct PerSocketData { int _; };

void optmize_can_util(hardware::TalonFX *motor) {
    motor->ResetSignalFrequencies();
    motor->OptimizeBusUtilization(0_Hz);
    motor->GetVelocity().SetUpdateFrequency(CAN_UPDATE_FREQ);
    motor->GetDeviceTemp().SetUpdateFrequency(CAN_UPDATE_FREQ);
    motor->GetSupplyVoltage().SetUpdateFrequency(CAN_UPDATE_FREQ);
    motor->GetSupplyCurrent().SetUpdateFrequency(CAN_UPDATE_FREQ);
}

double tps_to_wheel_speed(double tps) {
    return tps * GEARBOX_RATIO * GEARBOX_SPROCKET / WHEEL_SPROCKET * WHEEL_DIAMETER * M_PI * 3.6;
}

MotorData get_motor_data(hardware::TalonFX *motor) {
    double motor2_tps = motor->GetVelocity().GetValueAsDouble();
    return {
        .temp = motor->GetDeviceTemp().GetValueAsDouble(),
        .voltage = motor->GetSupplyVoltage().GetValueAsDouble(),
        .current = motor->GetSupplyCurrent().GetValueAsDouble(),
        .tps = motor2_tps,
        .wheelSpeed = tps_to_wheel_speed(motor2_tps),
    };
}

class TelemetryPlugin : public Plugin {
private:
    std::atomic<uWS::Loop *> event_loop{nullptr};
    std::atomic<uWS::App *> ws_app{nullptr};

    std::atomic<bool> should_broadcast{false};

    std::atomic<std::jthread *> ws_thread{nullptr};

    void run_ws() {
        uWS::Loop *ev_loop = uWS::Loop::get();
        event_loop.store(ev_loop);

        uWS::App *app = new uWS::App();
        ws_app.store(app);

        app->ws<PerSocketData>("/", {
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 16 * 1024,
            .idleTimeout = 8,
            .maxBackpressure = 1024 * 1024,
            .open = [](auto *ws) { ws->subscribe("telemetry"); },
            .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                if (message == "ping")
                    ws->send("pong", uWS::OpCode::TEXT);
            },
        });
        app->listen(SERVER_PORT, [this](auto *l_socket) {
            if (l_socket) {
                should_broadcast.store(true);
                logger->info("Websockets telemetry server listening on port {}", SERVER_PORT);
            } else
                logger->error("Websockets telemetry server is not listening");
        });

        app->run();
        delete app;
        
        ev_loop->free();
    }

public:
    TelemetryPlugin(const PluginContext *context) : Plugin(context) {
        ws_thread.store(new std::jthread(&TelemetryPlugin::run_ws, this));

        optmize_can_util(context->master);
        optmize_can_util(context->follower);
        logger->info("Finished setting can utilization");
    };

    uint32_t loop_micros() override {
        return 10'000;
    }

    void run() override {
        if (!should_broadcast.load())
            return;

        JsonData data = {
            .safeToRun = context->safe_to_run->load(),
            .throttle = context->throttle->load(),
            .targetTps = context->target_tps->load(),
            .motor1 = get_motor_data(context->master),
            .motor2 = get_motor_data(context->follower),
        };
        std::string buffer{};
        if (!glz::write_json(data, buffer)) {
            event_loop.load()->defer([this, packet = std::move(buffer)]() mutable {
                ws_app.load()->publish("telemetry", packet, uWS::OpCode::TEXT);
            });
        }
    }

    void stop() override {
        should_broadcast.store(false);

        if (event_loop.load()) {
            event_loop.load()->defer([this]() {
                uWS::App *app = ws_app.load();
                if (app) {
                    app->close();
                    logger->info("Telemetry server stopped");
                }
            });
        }
        delete ws_thread.load();
    }
};

ADD_PLUGIN_SYMBOLS(TelemetryPlugin)