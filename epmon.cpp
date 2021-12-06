// epmon.cpp
// This is the main module of the project described in EndpointAgentInterviewProject11.2021.pdf.
// The work is done by two classes, MonitorConfig (for reading the list of apps to monitor), and
// Monitor (for getting process info and sending it to the results server). The main function
// checks for loop interval parameters passed on the command line (with some simple error handling)
// but has default values, so command line parameters are not required.
// There are two data objects defined: a vector of strings to contain the names of applications
// to monitor, and a mutex for data locking. See the class implementation files for more on the
// use of this shared data.
// We instantiate an object of each of the two classes, passing in the loop interval value, a pointer
// to the app name vector, and the mutex. Then it starts the work loop in each class as a thread and
// just waits for the threads to finish. But there is no clever shutdown mechanism, so we catch
// Ctrl-C and terminate the program.
// What It Doesn't Do
// There is no way to update the work loop intervals for either class at runtime. They are
// passed in the constructors and that's it for the object's lifetime.
// The URLs for the configuration server and results server are hard-coded. They should both
// be configurable and updatable at runtime, but as with the loop intervals, there is no
// mechanism to do this.
// There are no test methods defined and there really should be.

#include <iostream>
#include <csignal>
#include <climits>
#include <mutex>
#include <thread>
#include "nlohmann_json/json.hpp"
#include "process_info.h"
#include "monitor_config.h"
#include "monitor.h"

using json = nlohmann::json;

// Define some reasonable min, max, and default values for loop intervals.
#define  CONFIG_UPDATE_INTERVAL_MIN 1
#define  CONFIG_UPDATE_INTERVAL_MAX 600
#define  CONFIG_UPDATE_INTERVAL_DEFAULT 30
#define  MONITOR_UPDATE_INTERVAL_MIN 1
#define  MONITOR_UPDATE_INTERVAL_MAX 600
#define  MONITOR_UPDATE_INTERVAL_DEFAULT 5

// This is the list of applications to monitor. It is shared by the MonitorConfig
// and Monitor classes.
std::vector<std::string> app_list;
// This is the mutex shared by the MonitorConfig and Monitor classes.
std::mutex data_lock;

// This struct stores program configuration, values are collected from the command line.
struct Ep_config
{
    // Interval in seconds to read/update the monitor configuration.
    int config_update_interval;
    // Interval in seconds to get process information and send the results.
    int monitor_interval;

    Ep_config() {
        config_update_interval = CONFIG_UPDATE_INTERVAL_DEFAULT;
        monitor_interval = MONITOR_UPDATE_INTERVAL_DEFAULT;
    }
    // default, nothing to do here
    ~Ep_config() = default;
};

namespace {
    // Look for configuration parameters on the command line. The expected format is
    // epmon [config read interval] [monitor interval]
    // where the interval values are seconds such that:
    //     1 <= [config read interval] <= 600
    //     1 <= [monitor interval] <= 600
    // If we fail to read values, use the default values defined above.
    // This is really brain-dead brute force parameter handling, but when we leave
    // this function we have appropriate config values.
    void read_program_config(int argc, char *argv[], Ep_config &cfg)
    {
        // If argc is zero, there's no need to check anything, we'll just use
        // the initialized default values.
        if (argc > 1) {
            if (argc != 3) {
                std::cerr << "ERROR: invalid number of parameters (" << argc - 1 << "). Expected two values, the configuration update interval\n"
                          << "and the monitor interval. Using default values (" << CONFIG_UPDATE_INTERVAL_DEFAULT << ", "
                          << MONITOR_UPDATE_INTERVAL_DEFAULT << "), respectively." << std::endl;
                return;
            }
            char *endptr;
            // We're expecting only 2 parameters, so let's just brute-force this.
            errno = 0;
            long argval = strtol(argv[1], &endptr, 10);
            if ((errno == ERANGE && (argval == LONG_MAX || argval == LONG_MIN)) ||
                (errno != 0 && argval == 0)) {
                std::cerr << "ERROR: invalid value (" << argval << ") for configuration update interval.\n"
                          << "Using default value (" << CONFIG_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = CONFIG_UPDATE_INTERVAL_DEFAULT;
            }
            if (endptr == argv[1]) {
                std::cerr << "ERROR: invalid value (" << argval << ") for configuration update interval.\n"
                          << "Using default value (" << CONFIG_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = CONFIG_UPDATE_INTERVAL_DEFAULT;
            }
            if (argval < CONFIG_UPDATE_INTERVAL_MIN) {
                std::cerr << "ERROR: Configuration update interval (" << argval << ") is less than allowed minimum value ("
                          << CONFIG_UPDATE_INTERVAL_MIN << ").\nUsing default value (" << CONFIG_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = CONFIG_UPDATE_INTERVAL_DEFAULT;
            }
            if (argval > CONFIG_UPDATE_INTERVAL_MAX) {
                std::cerr << "ERROR: Configuration update interval (" << argval << ") is greater than allowed maximum value ("
                          << CONFIG_UPDATE_INTERVAL_MAX << ").\nUsing default value (" << CONFIG_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = CONFIG_UPDATE_INTERVAL_DEFAULT;
            }

            cfg.config_update_interval = (int) argval;
            errno = 0;
            argval = strtol(argv[2], &endptr, 10);
            if ((errno == ERANGE && (argval == LONG_MAX || argval == LONG_MIN)) ||
                (errno != 0 && argval == 0)) {
                std::cerr << "ERROR: invalid value (" << argval << ") for monitor update interval.\n"
                          << "Using default value (" << MONITOR_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = MONITOR_UPDATE_INTERVAL_DEFAULT;
            }
            if (endptr == argv[2]) {
                std::cerr << "ERROR: invalid value (" << argval << ") for monitor update interval.\n"
                          << "Using default value (" << MONITOR_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = MONITOR_UPDATE_INTERVAL_DEFAULT;
            }
            if (argval < MONITOR_UPDATE_INTERVAL_MIN) {
                std::cerr << "ERROR: Monitor interval (" << argval << ") is less than allowed minimum value ("
                          << MONITOR_UPDATE_INTERVAL_MIN << ").\nUsing default value (" << MONITOR_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = MONITOR_UPDATE_INTERVAL_DEFAULT;
            }
            if (argval > MONITOR_UPDATE_INTERVAL_MAX) {
                std::cerr << "ERROR: Monitor interval (" << argval << ") is greater than allowed maximum value ("
                          << MONITOR_UPDATE_INTERVAL_MAX << ").\nUsing default value (" << MONITOR_UPDATE_INTERVAL_DEFAULT << ")." << std::endl;
                argval = MONITOR_UPDATE_INTERVAL_DEFAULT;
            }
            cfg.monitor_interval = (int) argval;
        }
    }

    // Simple signal handler to attempt a vaguely graceful shutdown.
    void signalIntHandler(int signum)
    {
        std::cout << "Received Ctrl-C termination signal, shutting down." << std::endl;
        exit(signum);
    }

    // An unimplemented enhancement:
    // Use a configuration file instead of the command line to pass in the
    // configuration data. This would include the configuration and results
    // server URLs as well as the time intervals. Trap some other signal that
    // can be sent via the keyboard and re-read the configuration file. Note
    // that this requires modifications to both the MonitorConfig and Monitor
    // classes to be able to pass in the updated configuration values.
//    void signalXHandler(int signum)
//    {
//        // Re-read the configuration files.
//        // Pass the updated config data to the affected objects.
//    }

}

// The main function. I made the decision to always have configuration values so the program
// won't quit immediately if you don't provide them or provide invalid values. That said,
// there is no mechanism to change those values once the program is running. There is also
// no clever shutdown mechanism, instead I just catch Ctrl-C and terminate.
// All that happens here is that we create and initialize the two classes, MonitorConfig to
// read the apps to monitor, and Monitor to do the monitoring and sending of results, and start
// their work loop threads.
int main(int argc, char *argv[])
{
    int pid = 0;
    double pcpu = 0, mem = 0;
    Ep_config prog_config;

    // register signal SIGTERM and signal handler
    signal(SIGINT, signalIntHandler);

    // set program config options if provided
    read_program_config(argc, argv, prog_config);
    std::cout << "epmon: configuration update interval = " << prog_config.config_update_interval
              << ", monitor interval = " << prog_config.monitor_interval << ".\n"
              << "Press Ctrl-C to quit." << std::endl;

    // start monitor configuration thread
    MonitorConfig monitor_config(prog_config.config_update_interval, &app_list, data_lock);
    std::thread monitor_config_thread = monitor_config.run();

    // start monitor thread
    Monitor monitor(prog_config.monitor_interval, &app_list, data_lock);
    std::thread monitor_thread = monitor.run();

    monitor_config_thread.join();
    monitor_thread.join();
    return 0;
}
