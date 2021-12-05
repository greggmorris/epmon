// monitor_config.h
// This file defines the MonitorConfig class. See monitor_config.cpp for more information.

#ifndef EPMON_MONITOR_CONFIG_H
#define EPMON_MONITOR_CONFIG_H

#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"

using json = nlohmann::json;

class MonitorConfig {
public:
    MonitorConfig(int interval, std::vector<std::string> *app_list, std::mutex &mut);
    ~MonitorConfig() = default;     // Nothing to clean up.

    std::thread run() { return std::thread([this] { this->config_loop(); }); }

private:
    int read_interval;  // number of seconds between calls to read configuration
    std::vector<std::string> *apps;
    std::mutex &data_lock;
    void update_config();
    void config_loop();
};

#endif //EPMON_MONITOR_CONFIG_H
