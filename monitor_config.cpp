// monitor_config.cpp
// This file implements the MonitorConfig class methods and associated support functions.
// This class is responsible for periodically getting configuration information which
// consists of a list of process names to monitor. The process names are stored in a shared
// vector of strings. This vector is also used by the Monitor class to know which processes
// to monitor. When it's time to read from the configuration server, we do the GET operation,
// and if that succeeds, we lock the shared vector of app names and update it. We do as
// little as possible while the data is locked; no external functions or methods are called
// while we're locked.
// The only public method is the run() method, which starts a thread running the private
// config_loop() method. This method runs forever, getting the configuration info on an
// interval passed into the constructor.
// What It Doesn't Do
// There is no way to update the config info collection interval. The interval is
// passed into the constructor, there is no mechanism to update it at runtime.
// The configuration server URL is hard-coded. It should be configurable and updatable at
// runtime, but as with the loop interval, there is no mechanism to do this.
// I think error handling could be more robust, and that's something that would probably
// be made more obvious by more extensive testing.
// Testing
// There isn't any but there should be, obviously. I would like to be able to create
// a list of JSON objects with a variety of configurations to test. For example, a list
// that includes nonexistent applications, no applications at all, a large number of
// applications (real and otherwise), and so on. I'm not sure how best to do it,
// but maybe add a flag to get_config() that means just return the JSON object that
// was passed in instead of making the GET call.
// It would probably be useful to have public access to the various private methods.
// Not sure how this might be done other than with conditional compile statements in
// the source.
// Ideally we want a way to test that doesn't always require starting the work loop
// thread.

#include <iostream>
#include <curl/curl.h>
#include <unistd.h>
#include "monitor_config.h"

// An anonymous namespace for functions that don't need to be in the MonitorConfig class itself.
namespace {
    // This callback function puts the GET results data into a string.
    size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, std::string *data)
    {
        data->append((char *) ptr, size * nmemb);
        return size * nmemb;
    }

    // Make a GET request to the configuration URL and convert the returned data
    // to a JSON object.
    bool get_config(const std::string &url, json &json_config)
    {
        bool ret = false;
        CURL *curl;
        CURLcode res;

//        json_config = "{ \"applications\" : [\"bash\", \"firefox\", \"clangd\", \"evince\", \"nautilus\", \"chromium\"] }"_json;
//        json_config = "{ \"applications\" : [\"bash\", \"firefox\"] }"_json;
//        return true;

        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
//            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

            std::string response_string;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

            curl_easy_perform(curl);
            // Convert the GET results into a JSON object.
            if (!response_string.empty())
                json_config = json::parse(response_string);
            else
                std::cout << "get_config: response_string is empty\n";
            curl_easy_cleanup(curl);
            ret = true;
        }
        return ret;
    }
}

// The constructor doesn't really do any work, just sets local variables. I wanted
// to minimize the places where locking would be required, so I don't do the first
// GET until the thread is actually running.
MonitorConfig::MonitorConfig(int interval, std::string url, std::vector<std::string> *app_list, std::mutex &mut)
    : read_interval(interval), server_url(std::move(url)), apps(app_list), data_lock(mut)
{
}

// This method updates the shared list of application names to monitor. Note that it
// only attempts to lock and update the shared list if the GET call was successful.
void MonitorConfig::update_config()
{
    json cfg;

    if (get_config(server_url, cfg)) {
        std::lock_guard<std::mutex> lck { data_lock };
        apps->clear();
        for (auto &element: cfg["applications"]) {
            apps->push_back(element);
        }
    }
    else
        std::cout << "MonitorConfig::update_config: get_config failed\n";
}

// This is the thread function. It runs forever because I didn't want to spend the time
// working out a clever "stop" mechanism. Each time through the loop we attempt to GET
// a list of applications to monitor. If that GET succeeds, the application names are
// stored in a shared vector of strings.
void MonitorConfig::config_loop()
{
    std::cout << "begin MonitorConfig::config_loop\n";
    while(true) {
        std::cout << "MonitorConfig::config_loop: getting config\n";
        update_config();
        std::cout << "MonitorConfig::config_loop: apps to monitor: " << std::endl;
        for (auto &app : *apps)
            std::cout << "  " << app << std::endl;
        std::cout << "MonitorConfig::config_loop: sleeping for " << read_interval << " seconds"<< std::endl;
        sleep(read_interval);
    }
}