//
// Based on code from the following sources:
// https://ubuntuforums.org/showthread.php?t=657097
// https://raw.githubusercontent.com/fho/code_snippets/master/c/getusage.c

#include "process_info.h"

#include <proc/readproc.h>

#include <unistd.h>
#include <dirent.h>
#include <limits.h>

#include <iostream>
#include <cstdlib>
#include <cstring>


#define PROC_DIRECTORY "/proc/"
#define STAT_DIRECTORY  "/stat"
#define CASE_SENSITIVE      1
#define CASE_INSENSITIVE    0
#define MAX_CMDLINE_LEN     4096
#define MAX_PROCNAME_LEN    1024
#define MAX_STATPATH_LEN    128


struct pstat
{
    long unsigned int utime_ticks;
    long int cutime_ticks;
    long unsigned int stime_ticks;
    long int cstime_ticks;
    long unsigned int vsize;    // virtual memory size in bytes
    long unsigned int rss;      // Resident Set Size in bytes
    long unsigned int cpu_total_time;
};

// Return true if the given string contains only digits, false otherwise.
bool IsNumeric(const char *pstr)
{
    for ( ; *pstr; pstr++)
        if (*pstr < '0' || *pstr > '9')
            return false;
    return true;
}

// Look for the string needle in the string haystack. If case_sensitive, do a
// case-sensitive comparison, otherwise do a case-insensitive comparison. Return
// true if needle is found in haystack, false otherwise.
bool contains_proc_name(const char *haystack, const char *needle, bool case_sensitive)
{
    if (case_sensitive)
        return strstr(haystack, needle) != nullptr;
    else
        return strcasestr(haystack, needle) != nullptr;
}

// Given a process name, search the process directories in the /proc directory for
// a directory/process with a matching (or containing) name. If a match is found,
// convert the directory name to a PID and return it. Return -2 if an error occurs
// with the proc directory, or -1 if no matching process is found.
pid_t GetPIDbyName(const char *pproc_name, bool check_case)
{
    char cmdline_path[MAX_CMDLINE_LEN];
    char proc_name[MAX_PROCNAME_LEN];
    char *endptr = nullptr;
    auto pid = (pid_t) -1;
    struct dirent *dir_entry = nullptr;
    DIR *dir_proc = nullptr;

    dir_proc = opendir(PROC_DIRECTORY);
    if (dir_proc == nullptr)
    {
        perror("Unable to open the " PROC_DIRECTORY " directory.");
        return (pid_t) -2;
    }

    // Loop through the entries in the proc directory, looking for directories with
    // names consisting of only digits.
    while ( (dir_entry = readdir(dir_proc)) )
    {
        if (dir_entry->d_type == DT_DIR)
        {
            if (IsNumeric(dir_entry->d_name))
            {
                // Found a process directory. Read the cmdline file inside the directory
                // to find the executable name.
                strcpy(cmdline_path, PROC_DIRECTORY);
                strcat(cmdline_path, dir_entry->d_name);
                strcat(cmdline_path, "/cmdline");
//                printf("cmdline_path: %s\n", cmdline_path);

                FILE *cmdline_file = fopen(cmdline_path, "rt");  // open the file for reading text
                if (cmdline_file)
                {
                    // The command line could be really long, make sure we don't overflow the buffer.
                    fgets(proc_name, MAX_PROCNAME_LEN, cmdline_file);
                    // Only look at the first entry in the command line string, which should be
                    // the executable name.
                    char *p = strchr(proc_name, ' ');
                    if (p != nullptr)
                        *p = '\0';
//                    printf("Process name: %s\n", chrarry_NameOfProcess);
//                    printf("Pure Process name: %s\n\n", chrptr_StringToCompare);
                    // See if the process's executable name contains the process name we're
                    // looking for. The executable name probably contains the path info,
                    // which we may not have been given for the process we're looking for.
                    if (contains_proc_name(proc_name, pproc_name, check_case))
                    {
                        // Found a matching process name. The name of the containing
                        // directory is the PID, so convert it to a pid_t and return it.
                        errno = 0;
                        long pid_tmp = strtol(dir_entry->d_name, &endptr, 10);
                        if ((errno == ERANGE && (pid_tmp == LONG_MAX || pid_tmp == LONG_MIN)) ||
                            (errno != 0 && pid_tmp == 0)) {
                            fprintf(stderr, "ERROR: failed to extract PID: invalid range.\n");
                            pid_tmp = -1;
                        }
                        if (endptr == dir_entry->d_name) {
                            fprintf(stderr, "ERROR: failed to extract PID: no digits were found.\n");
                            pid_tmp = -1;
                        }
                        closedir(dir_proc);
                        return (pid_t) pid_tmp;
                    }
                }
            }
        }
    }
    closedir(dir_proc);
    return pid;
}

// Read the proc stat data for the given PID into the passed-in struct. Return 0
// if successful, -1 otherwise.
int get_proc_data(const pid_t pid, struct pstat *result)
{
    char stat_filepath[MAX_STATPATH_LEN];
    // Create the path to the process.
    snprintf(stat_filepath, MAX_STATPATH_LEN, "%s%d%s", PROC_DIRECTORY, pid, STAT_DIRECTORY);
    FILE *fprocess_stat = fopen(stat_filepath, "r");
    if (fprocess_stat == nullptr) {
        char msg[64];
        perror("Failed to open the process's stat directory ");
        return -1;
    }
    FILE *fstat = fopen("/proc/stat", "r");
    if (fstat == nullptr) {
        perror("Failed to open /proc/stat ");
        fclose(fstat);
        return -1;
    }
    // Read values from /proc/pid/stat
    bzero(result, sizeof(struct pstat));
    long int rss;
    if (fscanf(fprocess_stat, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu"
                       "%lu %ld %ld %*d %*d %*d %*d %*u %lu %ld",
               &result->utime_ticks, &result->stime_ticks,
               &result->cutime_ticks, &result->cstime_ticks,
               &result->vsize, &rss) == EOF) {
        fclose(fprocess_stat);
        return -1;
    }
    fclose(fprocess_stat);
    result->rss = rss * getpagesize();

    // Read the individual state time values.
    long unsigned int cpu_time[10];
    bzero(cpu_time, sizeof(cpu_time));
    if (fscanf(fstat, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
               &cpu_time[0], &cpu_time[1], &cpu_time[2], &cpu_time[3],
               &cpu_time[4], &cpu_time[5], &cpu_time[6], &cpu_time[7],
               &cpu_time[8], &cpu_time[9]) == EOF) {
        fclose(fstat);
        return -1;
    }
    fclose(fstat);
    // Sum up the time spent in the various states to get total CPU time.
    for (int ii = 0; ii < 10; ii++)
        result->cpu_total_time += cpu_time[ii];
    return 0;
}

// Given two sets of proc stat data, calculate the CPU usage between them and return
// the value as a percentage.
void calc_cpu_usage_pct(const struct pstat *cur_usage, const struct pstat *last_usage,
                        double *ucpu_usage, double *scpu_usage)
{
    const long unsigned int total_time_diff = cur_usage->cpu_total_time - last_usage->cpu_total_time;

    *ucpu_usage = 100.0 * ((cur_usage->utime_ticks + cur_usage->cutime_ticks)
                          - (last_usage->utime_ticks + last_usage->cutime_ticks))
                         / (double) total_time_diff;

    *scpu_usage = 100.0 * (((cur_usage->stime_ticks + cur_usage->cstime_ticks)
                           - (last_usage->stime_ticks + last_usage->cstime_ticks))) /
                         (double) total_time_diff;
}

// The external function to call to get information about the given process name.
// Returns the PID for the given process name, or -1 if no matching process is found,
// or -2 if there's an error dealing with the proc directory.
// The only way to get the CPU usage is to calculate it from two different times.
// We get the process's stat data, sleep for 1 second, then get the data again.
// With those two sets of data we can calculate CPU usage.
int get_proc_info(const std::string &proc_name, int *pid, double *pcpu, double *mem)
{
    int ret = 0;
    struct pstat before = { 0 }, after = { 0 };
    double ucpu_usage = 0.0, scpu_usage = 0.0;

    pid_t pid_tmp = GetPIDbyName(proc_name.c_str(), CASE_INSENSITIVE);
    *pid = (int) pid_tmp;
    *pcpu = 0.0;
    *mem = 0.0;
//    std::cout << "PID for " << proc_name << " = " << pid_tmp << std::endl;
    if (pid_tmp > 0) {
        ret = get_proc_data(pid_tmp, &before);
        if (ret != 0)
            return ret;
        sleep(1);
        ret = get_proc_data(pid_tmp, &after);
        if (ret != 0)
            return ret;
        calc_cpu_usage_pct(&after, &before, pcpu, &scpu_usage);
//        std::cout << "ucpu_usage = " << ucpu_usage << ", scpu_usage = " << scpu_usage << std::endl;
//        std::cout << "vsize = " << after.vsize << ", rss = " << after.rss << std::endl;
        *pcpu = ucpu_usage;
        *mem = static_cast<double>(after.vsize);
    }
    return 0;
}
#if 0
// System uptime and idle time are stored in /proc/uptime as two floating point numbers. All
// the other numbers we need for CPU usage calculation are unsigned long longs. We only care
// about uptime, but to get it into an unsigned long long we have to monkey with it a bit.
unsigned long long get_uptime()
{
    char ut_s[64];
    double ut_d = 0;
    unsigned long long uptime = 0;
    FILE *f_uptime = fopen("/proc/uptime", "rt");  // open the file for reading text
    if (f_uptime) {
        // read the first floating point number as a string
        fscanf(f_uptime, "%s", ut_s);
        printf("ut_s = %s\n", ut_s);
        // convert the string to a double
        ut_d = strtod(ut_s, nullptr);
        // multiply by 100 to get rid of the decimal point
        ut_d *= 100.0;
        printf("ut_d = %f\n", ut_d);
        // convert the double to an unsigned long long
        uptime = static_cast<unsigned long long>(ut_d);
        printf("uptime = %lld\n", uptime);
        // finally, divide by 100. Yes, we are losing precision here.
        uptime /= 100;
    }
    return uptime;
}

void thing(const std::string &proc_name)
{
    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLSTATUS);
    proc_t proc_info;
    memset(&proc_info, 0, sizeof(proc_info));

    char ut[64], it[64];
    unsigned long long uptime = get_uptime();

//    FILE *proc_uptime = fopen("/proc/uptime", "rt");  // open the file for reading text
//    if (proc_uptime) {
//        fscanf(proc_uptime, "%s %s", ut, it);
//        uptime = strtoull(ut, nullptr, 10);
//        idle = strtoull(it, nullptr, 10);
//        printf("uptime = %f, idle = %f\n", uptime, idle);
//    }

    printf("%20s:\t%5s\t%5s\t%5s\t%5s\t%6s\t%6s\n", "Cmd Name", "PID", "Mem", "utime", "stime", "cutime", "cstime");
    while (readproc(proc, &proc_info) != nullptr) {
//        printf("%20s:\t%5ld\t%5lld\t%5lld\n", proc_info.cmd, proc_info.resident, proc_info.utime, proc_info.stime);
        if (strcmp(proc_name.c_str(), proc_info.cmd) == 0) {
            printf("%20s:\t%5d\t%5ld\t%5lld\t%5lld\t%5lld\t%5lld\n", proc_info.cmd, proc_info.tgid, proc_info.resident,
                   proc_info.utime, proc_info.stime, proc_info.cutime, proc_info.cstime);
            break;
        }
    }
#if 0
    First we determine the total time spent for the process:

total_time = utime + stime

We also have to decide whether we want to include the time from children processes. If we do, then we add those values to total_time:

total_time = total_time + cutime + cstime

Next we get the total elapsed time in seconds since the process started:

seconds = uptime - (starttime / Hertz)

Finally we calculate the CPU usage percentage:

cpu_usage = 100 * ((total_time / Hertz) / seconds)
//user_util = 100 * (utime_after - utime_before) / (time_total_after - time_total_before);
//sys_util = 100 * (stime_after - stime_before) / (time_total_after - time_total_before);
#endif
    printf("uptime = %lld\n", uptime);
    unsigned long long utime_before, utime_after;
    unsigned long long stime_before, stime_after;
    unsigned long long total_before, total_after;

    total_before = proc_info.utime + proc_info.stime + proc_info.cutime + proc_info.cstime;
    utime_before = proc_info.utime;
    stime_before = proc_info.stime;
    sleep(1);
    total_after = proc_info.utime + proc_info.stime + proc_info.cutime + proc_info.cstime;
    utime_after = proc_info.utime;
    stime_after = proc_info.stime;

    printf("total_before = %lld, utime_before = %lld, stime_before = %lld\n", total_before, utime_before, stime_before);
    printf("total_after = %lld, utime_after = %lld, stime_after = %lld\n", total_after, utime_after, stime_after);

//    unsigned long long cpu_usage = 100 * ((total_time / ticks) / seconds);
//    unsigned long long cpu_usage = (total_time * 100 / ticks) / seconds;
//    printf("cpu_usage = %lld\n", cpu_usage);

#if 0
    printf("uptime = %lld\n", uptime);
    unsigned long long total_time = proc_info.utime + proc_info.stime;
    total_time = total_time + proc_info.cutime + proc_info.cstime;
    printf("total_time = %lld\n", total_time);
    long ticks = sysconf(_SC_CLK_TCK);
    printf("ticks = %ld\n", ticks);
    printf("proc_info.start_time = %lld\n", proc_info.start_time);
    unsigned long long secs = proc_info.start_time / ticks;
    printf("secs = %lld\n", secs);
    unsigned long long seconds = uptime - secs;
    printf("seconds = %lld\n", seconds);
//    unsigned long long cpu_usage = 100 * ((total_time / ticks) / seconds);
    unsigned long long cpu_usage = (total_time * 100 / ticks) / seconds;
    printf("cpu_usage = %lld\n", cpu_usage);
#endif
    closeproc(proc);
}
#endif

//-------------------------------------------------------------

