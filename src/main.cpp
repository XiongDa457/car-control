#include <ctre/phoenix6/controls/Follower.hpp>
#include <ctre/phoenix6/SignalLogger.hpp>
#include <ctre/phoenix/platform/Platform.hpp>

#include <spdlog/spdlog.h>

#include "helpers.hpp"
#include "server.hpp"
#include "globals.hpp"
#include "read.hpp"
#include "control.hpp"

int main() {
    unmanaged::LoadPhoenix();
    SignalLogger::EnableAutoLogging(false);

    sleep(8);

    reset_can();
    sleep(1);

    CANBus can_bus{"can0"};
    
    hardware::TalonFX master{0, can_bus};
    hardware::TalonFX follower{1, can_bus};

    configs::TalonFXConfiguration motor_config =
        configs::TalonFXConfiguration{}
            .WithCurrentLimits(
                configs::CurrentLimitsConfigs{}
                    .WithStatorCurrentLimit(120_A)
                    .WithStatorCurrentLimitEnable(true)
                    .WithSupplyCurrentLimit(80_A)
                    .WithSupplyCurrentLimitEnable(true)
                    .WithSupplyCurrentLowerLimit(40_A)
                    .WithSupplyCurrentLowerTime(2.0s)
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
    
    log_status(master.GetConfigurator().Apply(motor_config));
    log_status(follower.GetConfigurator().Apply(motor_config));
    optmize_can_util(&master);
    optmize_can_util(&follower);
    log_status(master.GetVelocity().SetUpdateFrequency(UPDATE_FREQ));
    log_status(master.GetSupplyVoltage().SetUpdateFrequency(UPDATE_FREQ));
    
    controls::Follower follow_request{0, false};
    follower.SetControl(follow_request);

    spdlog::info("Starting control & read threads\n");

    jthread read(read_thread);
    jthread control(control_thread, &can_bus, &master, &follower);
    jthread app(app_thread);
}