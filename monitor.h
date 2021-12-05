// monitor.h
// This file defines the Monitor class. See monitor.cpp for more information.

#ifndef EPMON_MONITOR_H
#define EPMON_MONITOR_H

#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"

using json = nlohmann::json;

class Monitor {
public:
    Monitor(int interval, std::vector<std::string> *app_list, std::mutex &mut);
    ~Monitor();

    std::thread run() { return std::thread([this] { this->work_loop(); }); }

private:
    int monitor_interval;
    std::vector<std::string> *shared_app_list;
    std::vector<std::string> local_app_list;
    std::mutex &data_lock;
    int update_app_list();
    json get_all_app_info();
    void work_loop();
};

#endif //EPMON_MONITOR_H
