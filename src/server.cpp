#include <spdlog/spdlog.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "server.hpp"
#include "globals.hpp"

void app_thread() {
    sleep(1);

    httplib::Server svr;
    svr.Get("/get", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json j;
        j["speed"] = speed.load();
        j["temp1"] = temp1.load();
        j["temp2"] = temp2.load();
        j["current"] = total_current.load();
        j["voltage"] = supply_voltage.load();
        j["delay"] = avg_diff.load();
        res.set_content(j.dump(), "application/json");
    });
    spdlog::info("Server listening on port 8080.");
    svr.listen("0.0.0.0", 8080);
}