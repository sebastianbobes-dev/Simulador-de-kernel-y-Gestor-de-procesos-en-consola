#ifndef PCB_H
#define PCB_H

#include <string>

enum class ProcessState {
    New,
    Ready,
    Running,
    Terminated
};

struct PCB {
    int pid;
    std::string name;
    ProcessState state;
    int arrival_time;
    int initial_burst;
    int remaining_burst;
    int priority;
    int first_start_time;
    int completion_time;
};

#endif
