#include <ctre/phoenix6/TalonFX.hpp>
#include <ctre/phoenix6/unmanaged/Unmanaged.hpp>
#include <spdlog/logger.h>

#include "plugin/interface.hpp"

using namespace ctre::phoenix;
using namespace ctre::phoenix6;

#define LERP_PER_ITER 0.01;

class ControlPlugin : public Plugin {
private:
    double current_target = 0;

    void initialize() override {
        configs::TalonFXConfiguration motor_config =
        configs::TalonFXConfiguration{}
            .WithCurrentLimits(
                configs::CurrentLimitsConfigs{}
                    .WithStatorCurrentLimit(120_A)
                    .WithStatorCurrentLimitEnable(true)
                    .WithSupplyCurrentLimit(80_A)
                    .WithSupplyCurrentLimitEnable(true)
                    .WithSupplyCurrentLowerLimit(40_A)
                    .WithSupplyCurrentLowerTime(2.0_s)
            )
            .WithMotorOutput(
                configs::MotorOutputConfigs{}
                    .WithPeakForwardDutyCycle(1.0)
                    .WithPeakReverseDutyCycle(0.0)
            )
            .WithSlot0(
                configs::Slot0Configs{}
                    .WithKV(0.12)
                    .WithKP(0.003)
            );
        
        context->master->GetConfigurator().Apply(motor_config);
        context->follower->GetConfigurator().Apply(motor_config);
    }

public:
    using Plugin::Plugin;

    void run() override {
        unmanaged::FeedEnable(50);

        double target = 0;
        if (context->safe_to_run->load()) target = context->throttle->load();

        if (target < current_target) current_target = target;
        else current_target += (target - current_target) * LERP_PER_ITER;

        units::turns_per_second_t tps{current_target * 100.0};
        controls::VelocityVoltage speed_control(tps);
        context->master->SetControl(speed_control.WithUpdateFreqHz(0_Hz).WithSlot(0));
    }
};

ADD_PLUGIN_SYMBOLS(ControlPlugin)