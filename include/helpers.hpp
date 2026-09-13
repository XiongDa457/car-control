#ifndef __HELPERS_HPP
#define __HELPERS_HPP

#include <ctre/phoenix6/TalonFX.hpp>
#include <unistd.h>

using namespace std;
using namespace ctre::phoenix;
using namespace ctre::phoenix6;

#define UPDATE_FREQ 20_Hz

void pin_thread(int core_id);

void set_priority(int priority);

double interpolate(int val);

int16_t read_val(int serial_port);

uint64_t get_micros();

void command();

void reset_can();

void log_status(StatusCode status);

const units::hertz_t update_freq = 20_Hz;
void optmize_can_util(hardware::TalonFX *motor);

#endif