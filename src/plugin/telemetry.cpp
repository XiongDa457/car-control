#include <spdlog/logger.h>

#include "plugin/interface.hpp"

using namespace ctre::phoenix6;

class TelemetryPlugin : public Plugin {
public:
    TelemetryPlugin(const PluginContext *context) : Plugin(context) {}

    void run() override {
    }
};

ADD_PLUGIN_SYMBOLS(TelemetryPlugin)