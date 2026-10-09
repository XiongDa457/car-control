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

struct PerSocketData {
    int _;
};

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
    std::atomic<uWS::Loop*> event_loop{nullptr};
    std::atomic<us_listen_socket_t*> listen_socket{nullptr};

    void initialize() override {
        optmize_can_util(context->master);
        optmize_can_util(context->follower);
    }

public:
    using Plugin::Plugin;

    void run() override {
        event_loop.store(uWS::Loop::get());

        {
            uWS::App app = uWS::App().ws<PerSocketData>("/connect", {
                .compression = uWS::SHARED_COMPRESSOR,
                .maxPayloadLength = 16 * 1024,
                .idleTimeout = 10,
                .maxBackpressure = 1 * 1024 * 1024,
                .upgrade = [](auto *res, auto *req, auto *context) {
                    res->template upgrade<PerSocketData>({},
                        req->getHeader("sec-websocket-key"),
                        req->getHeader("sec-websocket-protocol"),
                        req->getHeader("sec-websocket-extensions"),
                        context
                    );
                },
                .open = [](auto *ws) {
                },
                .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) { ws->send(message, opCode); },
                .drain = [](auto *ws) {},
                .ping = [](auto *ws, std::string_view message) {},
                .pong = [](auto *ws, std::string_view message) {},
                .close = [](auto *ws, int code, std::string_view message) {}
            }).listen(SERVER_PORT, [this](auto *token) {
                if (token) {
                    listen_socket.store(token);
                    logger->info("Websockets telemetry server listening on port {}", SERVER_PORT);
                }
            }).run();
            app.close();
        }

        event_loop.load()->free();
        logger->info("Telemetry server stopped");
    }

    void stop() override {
        event_loop.load()->defer([this]() {
            if (listen_socket.load())
                us_listen_socket_close(0, listen_socket.load());
        });
    }
};

ADD_PLUGIN_SYMBOLS(TelemetryPlugin)