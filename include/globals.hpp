#ifndef __GLOBALS_HPP
#define __GLOBALS_HPP

#include <atomic>
using namespace std;

inline atomic<int16_t> throttle{0};
inline atomic<uint64_t> last_update{0};

inline atomic<double> speed{0.0};
inline atomic<double> temp1{0.0}, temp2{0.0};
inline atomic<double> total_current{0.0};
inline atomic<double> supply_voltage{0.0};
inline atomic<uint64_t> avg_diff{0};

#endif