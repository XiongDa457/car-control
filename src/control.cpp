#include <thread>

#include <spdlog/spdlog.h>

#include "globals.hpp"
#include "control.hpp"
#include "helpers.hpp"

void control_thread(CANBus *can_bus, hardware::TalonFX *master, hardware::TalonFX *follower) {
    pin_thread(3);
    set_priority(75);

    auto next_wakeup = chrono::steady_clock::now();
    const auto interval = chrono::microseconds(10'000);

    int log_counter = 0;
    const int log_counter_max = 10;
    
    uint64_t diff_sum = 0;

    int last_status = 0;

    bool bad = false;
    while (true) {
        unmanaged::FeedEnable(50);

        uint64_t micros_diff = get_micros() - last_update.load();
        diff_sum += micros_diff;
        // if (micros_diff > 200'000) {
        //     spdlog::error("Bad throttle!");
        //     bad = true;
        // }

        double target_tps = 0;
        if (!bad) target_tps = (interpolate(throttle.load())) * 95.0;

        units::turns_per_second_t tps{target_tps};
        controls::VelocityDutyCycle speed_control(tps);
        StatusCode status = master->SetControl(speed_control.WithUpdateFreqHz(0_Hz).WithSlot(0));
        if (status.IsError()) {
            int status_code = static_cast<int>(status);
            log_status(status);
            last_status = status_code;
        }

        log_counter++;
        if (log_counter == log_counter_max) {
            log_counter = 0;

            double actual_tps = master->GetVelocity().GetValue().value();
            double wheel_rps = actual_tps * 0.224 / 35 * 12;
            speed.store(wheel_rps * 0.508 * 3.141592 * 3.6);

            temp1.store(master->GetDeviceTemp().GetValue().value());
            temp2.store(follower->GetDeviceTemp().GetValue().value());

            total_current.store(master->GetSupplyCurrent().GetValue().value() + follower->GetSupplyCurrent().GetValue().value());

            supply_voltage.store(master->GetSupplyVoltage().GetValue().value());
            
            avg_diff.store(diff_sum / log_counter_max);
            diff_sum = 0;
        }

        next_wakeup += interval;
        this_thread::sleep_until(next_wakeup);
    }
}