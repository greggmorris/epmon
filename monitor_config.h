//
// Created by gregg on 12/4/21.
//

#ifndef EPMON_MONITOR_CONFIG_H
#define EPMON_MONITOR_CONFIG_H

#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"

using json = nlohmann::json;

class MonitorConfig {
public:
    MonitorConfig(int interval, std::vector<std::string> *app_list, std::mutex &mut);
    ~MonitorConfig();
private:
    int read_interval;  // number of seconds between calls to read configuration
    std::vector<std::string> *apps;
    std::mutex &data_lock;
public:
    void config_loop();
    std::thread run() {
        return std::thread([this] { this->config_loop(); });
    }
//    [[noreturn]] void run();
    void update_config();
};

#endif //EPMON_MONITOR_CONFIG_H
