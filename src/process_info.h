// process_info.cpp
// See process_info.cpp for more information.

#ifndef EPMON_PROCESS_INFO_H
#define EPMON_PROCESS_INFO_H

#include <string>

int get_proc_info(const std::string &proc_name, int *pid, double *pcpu, double *mem);

#endif //EPMON_PROCESS_INFO_H
