#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <spdlog/logger.h>

#include "plugin/interface.hpp"

using namespace ctre::phoenix6;

#define ARDUINO_PORT "/dev/ttyAMA0"
#define BAUD_RATE B500000

#define RAW_THROTTLE_LOW 190
#define RAW_THROTTLE_HI 840
constexpr double RAW_THROTTLE_RANGE = RAW_THROTTLE_HI - RAW_THROTTLE_LOW;

double interpolate(int val) {
    if (val < RAW_THROTTLE_LOW) val = RAW_THROTTLE_LOW;
    else if (val > RAW_THROTTLE_HI) val = RAW_THROTTLE_HI;
    return pow((val - RAW_THROTTLE_LOW) / RAW_THROTTLE_RANGE, 2.0f);
}

class ReadPlugin : public Plugin {
private:
    std::atomic<bool> running{true};
    int arduino_fd;

    double read_val() {
        uint8_t upper, lower;
        read(arduino_fd, &upper, 1);
        read(arduino_fd, &lower, 1);
        return interpolate((upper << 7) | lower);
    }

public:
    using Plugin::Plugin;

    void run() override {
        arduino_fd = open(ARDUINO_PORT, O_RDONLY | O_NOCTTY | O_NDELAY);
        if (arduino_fd < 0) {
            logger->error("Error when opening serial port for arduino");
            return;
        }

        termios options;
        tcgetattr(arduino_fd, &options);

        cfmakeraw(&options);
        cfsetispeed(&options, BAUD_RATE);

        options.c_cc[VMIN] = 1;
        options.c_cc[VTIME] = 1;

        tcsetattr(arduino_fd, TCSANOW, &options);

        context->throttle->store(0);
        context->safe_to_run->store(true);

        int64_t error_counter = 0;
        int8_t fix_counter = 0;
        uint8_t single_byte;
        while (running.load()) {
            int res = read(arduino_fd, &single_byte, 1);
            if (res == 0) {
                fix_counter = 0;
                error_counter++;
                if (error_counter == 2) {
                    context->throttle->store(0);
                    context->safe_to_run->store(false);
                }
                if (error_counter % 10 == 2) logger->error("Not recieving signal from arduino");
                continue;
            }
            if (res < 0) {
                logger->error("Error when reading from arduino");
                break;
            }
            if (single_byte == 0x80) {
                if (error_counter >= 5) {
                    fix_counter++;
                    if (fix_counter >= 10) {
                        error_counter = 0;
                        context->safe_to_run->store(true);
                    }
                }
                context->throttle->store(read_val());
            }
        }

        context->safe_to_run->store(false);
        close(arduino_fd);
    }

    void stop() override {
        running.store(false);
    }
};

ADD_PLUGIN_SYMBOLS(ReadPlugin)