#ifndef __CONTROL_HPP
#define __CONTROL_HPP

#include <ctre/phoenix6/CANBus.hpp>
#include <ctre/phoenix6/TalonFX.hpp>
#include <ctre/phoenix6/unmanaged/Unmanaged.hpp>

using namespace std;
using namespace ctre::phoenix;
using namespace ctre::phoenix6;

void control_thread(CANBus *can_bus, hardware::TalonFX *master, hardware::TalonFX *follower);

#endif
