//
// Created by gregg on 12/4/21.
//

#ifndef EPMON_MONITOR_H
#define EPMON_MONITOR_H

#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"
// for convenience
using json = nlohmann::json;

class Monitor {
public:
    Monitor(int interval, std::vector<std::string> *app_list, std::mutex &mut);
    ~Monitor();

private:
    int monitor_interval;
    std::vector<std::string> *shared_app_list;
    std::vector<std::string> local_app_list;
    std::mutex &data_lock;
    int update_app_list();

public:
    void work_loop();
    std::thread run() {
        return std::thread([this] { this->work_loop(); });
    }
//        [[noreturn]] void run();
    json get_all_app_info();

};

#endif //EPMON_MONITOR_H
