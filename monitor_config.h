// monitor_config.h
// This file defines the MonitorConfig class. See monitor_config.cpp for more information.

#ifndef EPMON_MONITOR_CONFIG_H
#define EPMON_MONITOR_CONFIG_H

#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"
#include <spdlog/spdlog.h>

using json = nlohmann::json;

class MonitorConfig {
public:
    MonitorConfig(int interval, std::string url, std::vector<std::string> *app_list, std::mutex &mut);
    ~MonitorConfig() = default;     // Nothing to clean up.

    std::thread run() { return std::thread([this] { this->config_loop(); }); }

private:
    // Number of seconds between calls to read configuration.
    int read_interval;
    // URL of configuration server.
    std::string server_url;
    // The shared list of apps to monitor.
    std::vector<std::string> *apps;
    // The shared mutex.
    std::mutex &data_lock;
    // The logger.
    std::shared_ptr<spdlog::logger> logger;

    void update_config();
    void config_loop();
};

#endif //EPMON_MONITOR_CONFIG_H
