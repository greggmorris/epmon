//
// Created by gregg on 12/2/21.
//

#include <iostream>
#include <unistd.h>
#include <curl/curl.h>
#include "monitor.h"
#include "process_info.h"

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
        jres["healthcheck"] = jvec;
        std::cout << "combine_results: jres=" << jres.dump(2) << std::endl;
        return jres;
    }

    // Send a POST message containing the JSON app monitoring results to the results URL.
    bool send_app_results(json &json_results)
    {
        bool ret = false;
        CURL *curl;
        CURLcode res;

        /* In windows, this will init the winsock stuff */
        curl_global_init(CURL_GLOBAL_ALL);

        /* get a curl handle */
        curl = curl_easy_init();
        if (curl) {
            /* First set the URL that is about to receive our POST. This URL can
               just as well be a https:// URL if that is what should receive the
               data. */
            curl_easy_setopt(curl, CURLOPT_URL, "https://enbtrmfkj3vp.x.pipedream.net");
            /* Now specify the POST data */
            // Convert the JSON results to a single string.

            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_results.dump(2).c_str());
            /* Perform the request, res will get the return code */
            res = curl_easy_perform(curl);
            /* Check for errors */
            if (res != CURLE_OK)
                fprintf(stderr, "curl_easy_perform() failed: %s\n",
                        curl_easy_strerror(res));
            /* always cleanup */
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();
#if 0
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
#endif
        return ret;
    }

}

// The constructor doesn't really do any work, just sets local variables. I wanted
// to minimize the places where locking would be required, so I don't populate the
// local app list from the shared app list until the thread is actually running.
Monitor::Monitor(int interval, std::vector<std::string> *app_list, std::mutex &mut)
    : monitor_interval(interval), shared_app_list(app_list), data_lock(mut)
{
}

// We don't really need to clear the local app list, so there's probably no reason
// why I couldn't just use the default destructor here.
Monitor::~Monitor() {
    local_app_list.clear();
}

// Copy from the shared list of applications to monitor to a local list. I went round
// and round on this, finally deciding that it would be better if I didn't have to lock
// the shared list for any longer than absolutely necessary. One possible drawback to
// this is that the list of applications to monitor could change while I'm still
// processing the local list. On the other hand, not having to worry about the list
// changing while I'm looping through it does simplify things a bit, I think.
int Monitor::update_app_list()
{
    local_app_list.clear();
    std::lock_guard<std::mutex> lck { data_lock };
    for (auto &app : *shared_app_list) {
        local_app_list.push_back(app);
    }
    return (int) local_app_list.size();
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
        std::cout << "  " << app << std::endl;
        get_proc_info(app, &pid, &pcpu, &mem);
        results_vec.push_back(make_single_result(app, pid, pcpu, mem));
    }
    jres = combine_results(results_vec);
    return jres;
}

// The thread function, the work loop of the Monitor class. It runs forever because I
// didn't want to spend the time working out a clever "stop" mechanism. Each time
// through the loop we update our local copy of the list of applications to monitor,
// then get the process info for each. We create a single JSON object with the results
// and POST it to the results server, then nap until it's time to run again.
void Monitor::work_loop()
{
    json jres;

    std::cout << "begin Monitor::run\n";
    while(true) {
        // Copy the shared app list to a local list.
        update_app_list();
        std::cout << "Monitor::run: calling get_all_app_info" << std::endl;
        // Get the lowdown on the apps to monitor in a single JSON object.
        jres = get_all_app_info();
        std::cout << "Monitor::run: calling send_app_results" << std::endl;
        // Send the collected results to the results server.
        send_app_results(jres);
        std::cout << "Monitor::run: sleeping for " << monitor_interval << " seconds"<< std::endl;
        // Sleep for the configured interval.
        sleep(monitor_interval);
    }
}
