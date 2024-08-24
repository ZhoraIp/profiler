#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

#define BUFFER_SIZE (20 * 1024) // Mmap buffer size 

inline void error_and_exit(const std::string &msg) {
    perror(msg.c_str());
    exit(EXIT_FAILURE);
}

inline int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

#endif // UTILS_H

