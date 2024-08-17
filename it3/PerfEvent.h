#ifndef PERFEVENT_H
#define PERFEVENT_H

#include <iostream>
#include <cstring>
#include <unordered_map>
#include <map>
#include "utils.h"

class PerfEvent {
public:
    int fd;
    std::string event_name;
    bool is_sampling;
    void *mmap_buffer;
    pid_t pid;
    uint64_t sample_period;

    PerfEvent(const std::string &event_name, bool is_sampling, pid_t pid, uint64_t sample_period = 0);
    ~PerfEvent();

    void read_count();
    void read_samples(std::unordered_map<int, PerfEvent*> &events_map, std::unordered_map<std::string, int> &global_histogram, std::map<uint64_t, std::pair<uint64_t, std::string>> &global_mmap_records, std::unordered_map<uint64_t, int> &global_ip_histogram);
};

#endif // PERFEVENT_H

