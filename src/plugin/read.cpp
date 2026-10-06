#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include "plugin/interface.hpp"

using namespace ctre::phoenix6;

#define ARDUINO_PORT "/dev/ttyAMA0"
#define BAUD_RATE B500000

#define RAW_THROTTLE_LOW 190
#define RAW_THROTTLE_HI 840
constexpr double RAW_THROTTLE_RANGE = RAW_THROTTLE_HI - RAW_THROTTLE_LOW;

#define TIMEOUT_MS 10
#define UNSAFE_ERROR_COUNT 5
#define RE_LOG_COUNT 100
#define FIX_COUNT 50

double interpolate(int val) {
    if (val < RAW_THROTTLE_LOW)
        val = RAW_THROTTLE_LOW;
    else if (val > RAW_THROTTLE_HI)
        val = RAW_THROTTLE_HI;
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

        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;
        options.c_cflag |= (CLOCAL | CREAD);

        tcsetattr(arduino_fd, TCSANOW, &options);

        struct pollfd pfd;
        pfd.fd = arduino_fd;
        pfd.events = POLLIN;

        int64_t error_counter = 0;
        int8_t fix_counter = 0;
        uint8_t single_byte;

        context->throttle->store(0);
        context->safe_to_run->store(true);
        while (running.load()) {
            pfd.revents = 0;

            int res = poll(&pfd, 1, TIMEOUT_MS);
            if (res > 0 && pfd.revents & POLLIN) {
                if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    logger->critical("Arudino device error!");
                    break;
                }
                size_t length = read(arduino_fd, &single_byte, 1);
                if (length > 0 && single_byte == 0x80) {
                    if (error_counter > 0) {
                        fix_counter++;
                        if (fix_counter >= FIX_COUNT) {
                            error_counter = 0;
                            if (!context->safe_to_run->load()) {
                                context->safe_to_run->store(true);
                                logger->info("Recieving signal from arduino again");
                            }
                        }
                    }
                    context->throttle->store(read_val());
                    continue;
                }
            }
            
            fix_counter = 0;
            error_counter++;
            if (error_counter == UNSAFE_ERROR_COUNT) {
                context->throttle->store(0);
                context->safe_to_run->store(false);
            }
            if (error_counter % RE_LOG_COUNT == UNSAFE_ERROR_COUNT)
                logger->critical("Not recieving signal from arduino");   
        }

        context->throttle->store(0);
        context->safe_to_run->store(false);
        close(arduino_fd);
    }

    void stop() override {
        running.store(false);
    }
};

ADD_PLUGIN_SYMBOLS(ReadPlugin)