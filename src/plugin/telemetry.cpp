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
    std::atomic<uWS::App *> app_ptr{nullptr};

    std::atomic<bool> ready{false};

    void initialize() override {
        optmize_can_util(context->master);
        optmize_can_util(context->follower);
    }

    void broadcast_thread(std::stop_token stoken) {
        uWS::Loop *ev_loop = event_loop.load();
        uWS::App *app = app_ptr.load();

        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!ready.load())
                continue;

            JsonData data = {
                .safeToRun = context->safe_to_run->load(),
                .throttle = context->throttle->load(),
                .targetTps = context->target_tps->load(),
                .motor1 = get_motor_data(context->master),
                .motor2 = get_motor_data(context->follower),
            };
            std::string buffer{};
            if (!glz::write_json(data, buffer)) {
                ev_loop->defer([&app, packet = std::move(buffer)]() mutable {
                    app->publish("telemetry", packet, uWS::OpCode::TEXT);
                });
            }
        }
    }

public:
    using Plugin::Plugin;

    void run() override {
        uWS::Loop *ev_loop = uWS::Loop::get();
        event_loop.store(ev_loop);

        uWS::App *app = new uWS::App();
        app_ptr.store(app);

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
                ready.store(true);
                logger->info("Websockets telemetry server listening on port {}", SERVER_PORT);
            } else
                logger->error("Websockets telemetry server is not listening");
        });

        std::jthread *broadcast = new std::jthread(&TelemetryPlugin::broadcast_thread, this);
        app->run();

        delete broadcast;
        delete app;

        event_loop.load()->free();
        logger->info("Telemetry server stopped");
    }

    void stop() override {
        event_loop.load()->defer([this]() {
            uWS::App *app = app_ptr.load();
            if (app)
                app->close();
        });
    }
};

ADD_PLUGIN_SYMBOLS(TelemetryPlugin)