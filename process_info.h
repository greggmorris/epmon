//
// Created by gregg on 12/3/21.
//

#ifndef EPMON_PROCESS_INFO_H
#define EPMON_PROCESS_INFO_H

#include <string>

//int get_proc_info(const std::string &proc_name);
int get_proc_info(const std::string &proc_name, int *pid, double *pcpu, double *mem);

//void thing(const std::string &proc_name);
//void thing2(const std::string &proc_name);

#endif //EPMON_PROCESS_INFO_H
