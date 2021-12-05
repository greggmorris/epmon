#include <iostream>
#include <csignal>
#include <cstdarg>
#include <mutex>
#include <thread>

#include "nlohmann_json/json.hpp"
#include "process_info.h"
#include "monitor_config.h"
#include "monitor.h"

using json = nlohmann::json;

// questions:
// Is seconds reasonable granularity for loop intervals or do we need to support sub-second intervals?
// What are reasonable min/max/default values for loop intervals?
// How is the program terminated?
// How is the configuration server URL determined? Is it configurable (e.g. passed in via command line),
// or hard-coded?
// How is the results server URL determined? Is it configurable (e.g. passed in via command line),
// or hard-coded?


// /usr/share/atom/atom
// /usr/bin/nautilus
// /usr/lib/libreoffice/program/soffice.bin
// /usr/lib/firefox/firefox
// /usr/bin/gnome-shell
// /usr/lib/xorg/Xorg


extern bool get_config(json &json_config);

#define  CONFIG_UPDATE_INTERVAL_MIN 1
#define  CONFIG_UPDATE_INTERVAL_MAX 600
#define  CONFIG_UPDATE_INTERVAL_DEFAULT 10
#define  MONITOR_UPDATE_INTERVAL_MIN 1
#define  MONITOR_UPDATE_INTERVAL_MAX 600
#define  MONITOR_UPDATE_INTERVAL_DEFAULT 4

// This is the list of applications to monitor. It is shared by the MonitorConfig
// and Monitor classes.
std::vector<std::string> app_list;
// This is the mutex shared by the MonitorConfig and Monitor classes.
std::mutex data_lock;

// Program configuration, values are collected from the command line.
struct Ep_config
{
    // Interval in seconds to read/update the monitor configuration. Minimum interval
    // is 1 second, max is 3600 seconds, default is 5 seconds.
    int config_update_interval;
    int monitor_interval;

    Ep_config() {
        config_update_interval = CONFIG_UPDATE_INTERVAL_DEFAULT;
        monitor_interval = MONITOR_UPDATE_INTERVAL_DEFAULT;
    }
    // default, nothing really to do here
    ~Ep_config() = default;
};

namespace {
    bool read_program_config(int argc, char *argv[], Ep_config &cfg)
    {
        bool found = false;

        if (argc > 1) {
            found = true;
        }
        return found;
    }

    // Simple signal handler to attempt a vaguely graceful shutdown.
    void signalTermHandler(int signum)
    {
        std::cout << "Termination signal (" << signum << ") received.\n";
        // cleanup and close up stuff here
        // terminate program
        exit(signum);
    }

    // An unimplemented enhancement:
    // Use a configuration file instead of the command line to pass in the
    // configuration data. This would include the configuration and results
    // server URLs as well as the time intervals. Trap SIGINT and re-read the
    // configuration file. Note that this requires modifications to both
    // the MonitorConfig and Monitor classes to be able to pass in the updated
    // configuration values.
//    void signalIntHandler(int signum)
//    {
//        // Re-read the configuration files.
//        // Pass the updated config data to the affected objects.
//    }

}

int main(int argc, char *argv[])
{
    int pid = 0;
    double pcpu = 0, mem = 0;
    Ep_config prog_config;

    std::cout << "begin epmon" << std::endl;
    // register signal SIGTERM and signal handler
    signal(SIGTERM, signalTermHandler);

    // set program config options if provided
    if (read_program_config(argc, argv, prog_config)) {
        std::cout << "Using the following configuration values:" << std::endl;
    }
    else
        std::cout << "Using default configuration values." << std::endl;

    // start monitor configuration thread
    MonitorConfig monitor_config(prog_config.config_update_interval, &app_list, data_lock);
    std::thread monitor_config_thread = monitor_config.run();

    // start monitor thread
    Monitor monitor(prog_config.monitor_interval, &app_list, data_lock);
    std::thread monitor_thread = monitor.run();

    monitor_config_thread.join();
    monitor_thread.join();
    std::cout << "end epmon\n";
    return 0;
}
