// monitor.cpp
// This file implements the Monitor class methods and associated support functions.
// This class is responsible for periodically getting process information about a
// list of processes. The process info is collected into a single JSON object and
// send to a results server.
// When it's time to get process info, the first thing we do is lock the shared list
// of app names and copy them to a local list. I felt that was the safest / easiest way
// to deal with the possibility of the list changing while we're looping through it,
// and to minimize the time spent with the data locked.
// Once we have an updated list of app names, we loop through it, getting the process
// info for the app (if the app exists) and creating a JSON object with the results.
// The results of each app are added to a local vector of JSON objects, then when we
// have collected info on all the apps, the individual results are combined into a
// single JSON object. That object is converted to a string and sent via a POST message
// to the results server.
// The only public method is the run() method, which starts a thread running the
// private work_loop() method. This method runs forever, getting process information
// on an interval passed into the constructor.
// What It Doesn't Do
// There is no way to update the process info collection interval. The interval is
// passed into the constructor, there is no mechanism to update it at runtime.
// Likewise the results server URL is passed into the constructor and there is no
// mechanism to update it at runtime, either. Both of these should be configurable
// and updatable at runtime.
// I think error handling could be more robust, which is something that would probably
// be made more obvious by more extensive testing.
// Testing
// There isn't any but there should be, obviously. I think it would be useful to be
// able to create a set of process info without an actual corresponding process. Then
// we could mess with that output and make sure the rest of the code Does The Right Thing.
// As with the MonitorConfig class, it would probably be useful to have public access
// to the various private methods. Not sure how this might be done other than with
// conditional compile statements in the source.
// Ideally we want a way to test that doesn't always require starting the work loop
// thread.

#include <unistd.h>
#include <curl/curl.h>
#include "monitor.h"
#include "process_info.h"

// This struct is used by the curl_output_cb function to store the response data
// from a call to curl_easy_perform().
struct curl_response {
    char *response;
    size_t size;
};

// An anonymous namespace for functions that don't need to be in the Monitor class itself.
namespace {
    // A simple function to combine process info into a single JSON object in the form
    // {
    //   "app": "bash",
    //   "timestamp": true,
    //   "PID": 2544,
    //   "CPU": 0.264,
    //   "Memory": 13852672.0
    // }
    json make_single_result(std::string proc_name, int pid, double pcpu, double mem)
    {
        // create the timestamp string
        auto right_now = std::chrono::system_clock::now();
        std::time_t the_time = std::chrono::system_clock::to_time_t(right_now);
        std::string timestamp(std::ctime(&the_time));
        // If there's a trailing newline on the timestamp, get the time string without it.
        if (timestamp.at(timestamp.length() - 1) == '\n')
            timestamp = timestamp.substr(0,timestamp.length() - 1);
        json jres = {
                {"app", proc_name},
                {"timestamp", timestamp},
                {"PID", pid},
                {"CPU", pcpu},
                {"Memory", mem},
        };
        return jres;
    }

    // Simple function to create a single JSON object from a vector of JSON objects in the form
    // { "healthcheck" : [
    //   { "app": "bash",
    //     "timestamp": true,
    //     "PID": 2544,
    //     "CPU": 0.264,
    //     "Memory": 13852672.0 },
    //   { "app": "nautilus",
    //      ... },
    //   ...
    //   ]
    // }
    json combine_results(std::vector<json> &results_vec)
    {
        json jres, jvec(results_vec);
        if (!results_vec.empty()) {
            jres["healthcheck"] = jvec;
        }
        return jres;
    }

    // This callback function captures the output of the curl_easy_perform() call.
    // Without it, the output is written to stdout, which is not helpful in this case.
    size_t curl_output_cb(void *data, size_t size, size_t nmemb, void *userp)
    {
        size_t realsize = size * nmemb;
        auto *mem = (struct curl_response *)userp;
        char *ptr = (char *)realloc(mem->response, mem->size + realsize + 1);
        if (ptr == nullptr)
            return 0;  /* out of memory! */
        mem->response = ptr;
        memcpy(&(mem->response[mem->size]), data, realsize);
        mem->size += realsize;
        mem->response[mem->size] = 0;
        return realsize;
    }

    // Send a POST message containing the JSON app monitoring results to the results URL.
    bool send_app_results(const std::string &url, json &json_results, const std::shared_ptr<spdlog::logger> &logger)
    {
        bool ret = false;
        char post_buf[4096];
        CURL *curl;
        CURLcode res;
        struct curl_response resp = { nullptr };

        curl = curl_easy_init();
        if (curl) {
            // Set the results server URL. Hard-coded for now but really should be configurable.
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_output_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&resp);
            // Convert the JSON results to a single string.
            strncpy(post_buf, json_results.dump().c_str(), 4096);
            // Pass the JSON string as the POST data.
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_buf);
            // POST it!
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
                logger->error("Monitor send_app_results failed: {}", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            ret = (res == CURLE_OK);
        }
        else
            logger->error("Monitor send_app_results curl_easy_init failed");
        return ret;
    }
}

// The constructor doesn't really do any work, just sets local variables. I wanted
// to minimize the places where locking would be required, so I don't populate the
// local app list from the shared app list until the thread is actually running.
Monitor::Monitor(int interval, std::string url, std::vector<std::string> *app_list, std::mutex &mut)
    : monitor_interval(interval), results_url(std::move(url)), shared_app_list(app_list), data_lock(mut)
{
    // Get the shared logger pointer.
    logger = spdlog::get("epmon");
}

// We don't really need to clear the local app list, so there's probably no reason
// why I couldn't just use the default destructor here.
Monitor::~Monitor() {
    local_app_list.clear();
}

// Loop through the (local) list of applications to monitor and get the desired
// process info for each. Combine the individual results into a single JSON object
// and return it.
json Monitor::get_all_app_info()
{
    int pid = 0;
    double pcpu = 0.0, mem = 0.0;
    json jres;
    std::vector<json> results_vec;

    for (auto &app : local_app_list) {
        logger->info("Monitor::get_all_app_info: getting info for {0}", app);
        get_proc_info(app, &pid, &pcpu, &mem);
        // Only add results if we actually got some.
        if (pid > 0) {
            jres = make_single_result(app, pid, pcpu, mem);
            results_vec.push_back(jres);
        }
        else
            logger->warn("Monitor::get_all_app_info: process {0} not found", app);
    }
    // Combine the individual results into a single JSON object. If there are no
    // results to combine, jres will be empty.
    jres = combine_results(results_vec);
    return jres;
}

// Copy from the shared list of applications to monitor to a local list. I went round
// and round on this, finally deciding that it would be better if I didn't have to lock
// the shared list for any longer than absolutely necessary. One drawback to
// this is that if the list of applications to monitor changes while I'm still
// processing the local list, I won't know about it until the next time through the
// work loop. It's also a duplication of data, but I don't think it's likely to be
// a huge amount of data.
int Monitor::update_app_list()
{
    local_app_list.clear();
    std::lock_guard<std::mutex> lck { data_lock };
    for (auto &app : *shared_app_list) {
        local_app_list.push_back(app);
    }
    return (int) local_app_list.size();
}

// This is the thread function. It runs forever because I didn't want to spend the time
// working out a clever "stop" mechanism. Each time through the loop we update our local
// copy of the list of applications to monitor, then get the process info for each. We
// create a single JSON object with the results and POST it to the results server, then
// nap until it's time to run again.
void Monitor::work_loop()
{
    json jres;

    logger->info("begin Monitor::work_loop");
    while(true) {
        // Copy the shared app list to a local list.
        logger->info("Monitor::work_loop: calling update_app_list");
        int num_apps = update_app_list();
        // It's possible there are no apps to monitor. This may happen if this thread
        // runs before the MonitorConfig thread can read the app list, or maybe
        // Something Bad happened reading from the configuration server. Whatever,
        // we'll just sleep and hope things are better next time around.
        if (num_apps == 0) {
            logger->warn("Monitor::work_loop: no apps specified");
        }
        else {
            // Get the process info for the monitored apps in a single JSON object.
            jres = get_all_app_info();
            if (jres.empty()) {
                logger->warn("Monitor::work_loop: no results to send");
            }
            else {
                // Send the collected results to the results server.
                bool ret = send_app_results(results_url, jres, logger);
                if (ret)
                    logger->info("Monitor::work_loop: successfully sent app monitor results");
                else
                    logger->warn("Monitor::work_loop: failed to send app monitor results");
            }
        }
        logger->info("Monitor::work_loop: sleeping for {0} seconds", monitor_interval);
        // Sleep for the configured interval.
        sleep(monitor_interval);
    }
}
