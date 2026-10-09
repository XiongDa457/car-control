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

class TelemetryPlugin : public Plugin {
private:
    std::atomic<uWS::Loop *> event_loop{nullptr};
    std::atomic<us_listen_socket_t *> listen_socket{nullptr};

    void initialize() override {
        optmize_can_util(context->master);
        optmize_can_util(context->follower);
    }

public:
    using Plugin::Plugin;

    void run() override {
        uWS::Loop *ev_loop = uWS::Loop::get();
        event_loop.store(ev_loop);

        std::atomic<bool> ready{false};
        uWS::App *app = new uWS::App();

        app->ws<PerSocketData>("/", {
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 16 * 1024,
            .idleTimeout = 8,
            .maxBackpressure = 1024 * 1024,
            .open = [](auto *ws) { ws->subscribe("telemetry"); },
        });
        app->listen(SERVER_PORT, [this, &ready](auto *l_socket) {
            if (l_socket) {
                listen_socket.store(l_socket);
                ready.store(true);
                logger->info("Websockets telemetry server listening on port {}", SERVER_PORT);
            } else
                logger->error("Websockets telemetry server is not listening");
        });

        std::jthread *broadcast = new std::jthread([this, &ev_loop, &ready, &app](std::stop_token stoken) {
            while (!stoken.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (!ready.load())
                    continue;

                double motor1_tps = context->master->GetVelocity().GetValueAsDouble();
                double motor2_tps = context->follower->GetVelocity().GetValueAsDouble();
                JsonData data = {
                    .safeToRun = context->safe_to_run->load(),
                    .throttle = context->throttle->load(),
                    .targetTps = context->target_tps->load(),
                    .motor1 = {
                        .temp = context->master->GetDeviceTemp().GetValueAsDouble(),
                        .voltage = context->master->GetSupplyVoltage().GetValueAsDouble(),
                        .current = context->master->GetSupplyCurrent().GetValueAsDouble(),
                        .tps = motor1_tps,
                        .wheelSpeed = tps_to_wheel_speed(motor1_tps),
                    },
                    .motor2 = {
                        .temp = context->master->GetDeviceTemp().GetValueAsDouble(),
                        .voltage = context->master->GetSupplyVoltage().GetValueAsDouble(),
                        .current = context->master->GetSupplyCurrent().GetValueAsDouble(),
                        .tps = motor2_tps,
                        .wheelSpeed = tps_to_wheel_speed(motor2_tps),
                    },
                };
                std::string buffer{};
                if (!glz::write_json(data, buffer)) {
                    ev_loop->defer([&app, packet = std::move(buffer)]() mutable {
                        app->publish("telemetry", packet, uWS::OpCode::TEXT);
                    });
                }
            }
        });

        app->run();

        delete broadcast;
        app->close();

        delete app;

        event_loop.load()->free();
        logger->info("Telemetry server stopped");
    }

    void stop() override {
        event_loop.load()->defer([this]() {
            us_listen_socket_t *l_socket = listen_socket.load();
            if (l_socket)
                us_listen_socket_close(0, l_socket);
        });
    }
};

ADD_PLUGIN_SYMBOLS(TelemetryPlugin)