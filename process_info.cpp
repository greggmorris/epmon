// process_info.cpp
// Most of the code here was modified in various ways by me, based on code from the following sources:
// https://ubuntuforums.org/showthread.php?t=657097
// https://raw.githubusercontent.com/fho/code_snippets/master/c/getusage.c
// It is mostly roughly polished C code, not really C++ at all.
// It is intended that the single entry point into this module is the get_proc_info() function.
// It is expected that this entire module be replaced with something more robust, but this will
// have to do for now. The thing that bothers me most is the need to do "before" and "after"
// calls to get proc data to calculate the percent of CPU used by the process. There should be
// a better way but I couldn't find it in a reasonable time frame.

#include "process_info.h"
#include <unistd.h>
#include <dirent.h>
#include <climits>
#include <cstdlib>
#include <cstring>

#define PROC_DIRECTORY      "/proc/"
#define STAT_DIRECTORY      "/stat"
#define CASE_SENSITIVE      1
#define CASE_INSENSITIVE    0
#define MAX_CMDLINE_LEN     4096
#define MAX_PROCNAME_LEN    1024
#define MAX_STATPATH_LEN    128

// This struct is used to store a subset of the data available in the /proc/<pid>/stat file.
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
// Memory use is reported in Kbytes.
int get_proc_info(const std::string &proc_name, int *pid, double *pcpu, double *mem)
{
    int ret = 0;
    struct pstat before = { 0 }, after = { 0 };
    double ucpu_usage = 0.0, scpu_usage = 0.0;

    pid_t pid_tmp = GetPIDbyName(proc_name.c_str(), CASE_INSENSITIVE);
    *pid = (int) pid_tmp;
    *pcpu = 0.0;
    *mem = 0.0;
    if (pid_tmp > 0) {
        ret = get_proc_data(pid_tmp, &before);
        if (ret != 0)
            return ret;
        sleep(1);
        ret = get_proc_data(pid_tmp, &after);
        if (ret != 0)
            return ret;
        calc_cpu_usage_pct(&after, &before, pcpu, &ucpu_usage);
        *pcpu = ucpu_usage;
        *mem = static_cast<double>(after.vsize) / 1024.0;
    }
    return 0;
}
