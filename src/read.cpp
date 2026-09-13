#include <fcntl.h>
#include <termios.h>

#include "globals.hpp"
#include "read.hpp"
#include "helpers.hpp"

#include <spdlog/spdlog.h>

void read_thread() {
    last_update.store(get_micros());

    pin_thread(2);
    set_priority(80);
    
    int arduino_port = open("/dev/ttyAMA0", O_RDONLY);
    if (arduino_port < 0) {
        spdlog::error("Failed to open serial port for arduino");
        return;
    }

    termios tty;
    tcgetattr(arduino_port, &tty);

    cfmakeraw(&tty);
    cfsetispeed(&tty, B500000);

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(arduino_port, TCSANOW, &tty);

    int error_count = 0;
    uint8_t single_byte;
    while (true) {
        if (read(arduino_port, &single_byte, 1) != 1) {
            error_count++;
            if (error_count == 10) {
                spdlog::error("Failed reading arduino serial >10 times");
                error_count = 0;
            }
            continue;
        }
        if (single_byte == 0x80) {
            throttle.store(read_val(arduino_port));
            last_update.store(get_micros());
        }
    }
}