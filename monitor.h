// monitor.h
// This file defines the Monitor class. See monitor.cpp for more information.

#ifndef EPMON_MONITOR_H
#define EPMON_MONITOR_H

#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"
#include <spdlog/spdlog.h>

using json = nlohmann::json;

class Monitor {
public:
    Monitor(int interval, std::string url, std::vector<std::string> *app_list, std::mutex &mut);
    ~Monitor();

    std::thread run() { return std::thread([this] { this->work_loop(); }); }

private:
    // Number of seconds between calls to get process info.
    int monitor_interval;
    // URL of results server.
    std::string results_url;
    // The shared list of apps to monitor.
    std::vector<std::string> *shared_app_list;
    // The local copy of the shared list of apps to monitor.
    std::vector<std::string> local_app_list;
    // The shared mutex.
    std::mutex &data_lock;
    // The logger.
    std::shared_ptr<spdlog::logger> logger;

    int update_app_list();
    json get_all_app_info();
    void work_loop();
};

#endif //EPMON_MONITOR_H
