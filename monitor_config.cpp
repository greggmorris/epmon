//
// Created by gregg on 12/2/21.
//
#include <iostream>

#include <curl/curl.h>
#include <unistd.h>
#include "monitor_config.h"

namespace {
    // Put the GET results data into a string.
    size_t writeFunction(void *ptr, size_t size, size_t nmemb, std::string *data) {
        data->append((char *) ptr, size * nmemb);
        return size * nmemb;
    }

    // Make a GET request to the configuration URL and convert the returned data
    // to a JSON object.
    bool get_config(json &json_config)
    {
        bool ret = false;
        CURL *curl;
        CURLcode res;

        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, "http://my-json-server.typicode.com/skatsev/testproject/posts/1");
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

            std::string response_string;
            std::string header_string;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);

            curl_easy_perform(curl);
//        std::cout << "response: " << response_string << "\n\n";
            // Convert the GET results into a JSON object.
            json_config = json::parse(response_string);
//        std::cout << "json: " << json_config.dump(4) << std::endl;
            curl_easy_cleanup(curl);
//        curl_global_cleanup();
            curl = nullptr;
            ret = true;
        }
        return ret;
    }
}

MonitorConfig::MonitorConfig(int interval, std::vector<std::string> *app_list, std::mutex &mut)
    : read_interval(interval), apps(app_list), data_lock(mut)
{
}

MonitorConfig::~MonitorConfig()
{
    apps->clear();
}

void MonitorConfig::update_config()
{
    json cfg;

    if (get_config(cfg)) {
        std::cout << "MonitorConfig::update_config: json: " << cfg.dump(4) << std::endl;
        std::lock_guard<std::mutex> lck { data_lock };
        apps->clear();
        for (auto &element: cfg["applications"]) {
            std::cout << "MonitorConfig::update_config: adding app " << element << std::endl;
            apps->push_back(element);
        }
    }
}

void MonitorConfig::config_loop()
{
    std::cout << "begin MonitorConfig::run\n";
    while(true) {
        std::cout << "MonitorConfig::run: getting config\n";
        update_config();
        std::cout << "MonitorConfig::run: apps: " << std::endl;
        for (auto &app : *apps)
            std::cout << "  " << app << std::endl;
        std::cout << "MonitorConfig::run: sleeping for " << read_interval << " seconds"<< std::endl;
        sleep(read_interval);
    }
}