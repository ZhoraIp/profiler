#include "PerfEvent.h"
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>


// Constructor for PerfEvent
PerfEvent::PerfEvent(const std::string &event_name, bool is_sampling, pid_t pid, uint64_t sample_period)
    : event_name(event_name), is_sampling(is_sampling), pid(pid), sample_period(sample_period) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.size = sizeof(struct perf_event_attr);
    pe.type = PERF_TYPE_HARDWARE;

    if (event_name == "instructions") {
        pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    } else if (event_name == "cycles") {
        pe.config = PERF_COUNT_HW_CPU_CYCLES;
    } else if (event_name == "cache-misses") {
        pe.config = PERF_COUNT_HW_CACHE_MISSES;
    } else {
        std::cerr << "Unsupported event type.\n";
        exit(EXIT_FAILURE);
    }

    if (is_sampling) {
        pe.sample_period = sample_period;
        pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID;
        pe.wakeup_events = 1;
    }

    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.task = 1; // To track FORK and EXIT events

    fd = perf_event_open(&pe, pid, -1, -1, 0);
    if (fd == -1) {
        error_and_exit("perf_event_open");
    }

    if (is_sampling) {
        mmap_buffer = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mmap_buffer == MAP_FAILED) {
            error_and_exit("mmap");
        }
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
}

// Destructor for PerfEvent
PerfEvent::~PerfEvent() {
    if (is_sampling && mmap_buffer) {
        munmap(mmap_buffer, BUFFER_SIZE);
    }
    close(fd);
}

// Read event count
void PerfEvent::read_count() {
    long long count;
    if (read(fd, &count, sizeof(long long)) == -1) {
        error_and_exit("read");
    }
    std::cout << "Event count (" << event_name << ") for PID " << pid << ": " << count << "\n";
}

// Read and process samples
void PerfEvent::read_samples(std::unordered_map<int, PerfEvent*> &events_map) {
    struct perf_event_mmap_page *header = (struct perf_event_mmap_page *)mmap_buffer;
    char *data = (char *)mmap_buffer + header->data_offset;
    uint64_t data_head = header->data_head;
    asm volatile("" ::: "memory");
    uint64_t data_tail = header->data_tail;

    while (data_tail != data_head) {
        struct perf_event_header *event = (struct perf_event_header *)(data + (data_tail & (BUFFER_SIZE - 1)));
        if (event->type == PERF_RECORD_SAMPLE) {
            uint64_t ip;
            memcpy(&ip, (char *)event + sizeof(struct perf_event_header), sizeof(uint64_t));
            uint32_t pid, tid;
            memcpy(&pid, (char *)event + sizeof(struct perf_event_header) + sizeof(uint64_t), sizeof(uint32_t));
            memcpy(&tid, (char *)event + sizeof(struct perf_event_header) + sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint32_t));
            std::cout << "IP: " << std::hex << ip << std::dec << " PID: " << pid << "\n";
        } else if (event->type == PERF_RECORD_FORK) {
            struct { uint32_t pid, ppid, tid, ptid; } fork;
            memcpy(&fork, (char *)event + sizeof(struct perf_event_header), sizeof(fork));
            std::cout << "FORK event: PID " << fork.pid << " PPID " << fork.ppid << "\n";

            // Create a new PerfEvent for the forked process
            PerfEvent* new_event = new PerfEvent(event_name, is_sampling, fork.pid, sample_period);
            events_map[new_event->fd] = new_event;
        } else if (event->type == PERF_RECORD_EXIT) {
            struct { uint32_t pid, ppid, tid, ptid; } exit;
            memcpy(&exit, (char *)event + sizeof(struct perf_event_header), sizeof(exit));
            std::cout << "EXIT event: PID " << exit.pid << " PPID " << exit.ppid << "\n";

            // Remove and delete the PerfEvent for the exited process
            for (auto it = events_map.begin(); it != events_map.end(); ++it) {
                if (it->second->pid == static_cast<pid_t>(exit.pid)) {
                    delete it->second;
                    events_map.erase(it);
                    break;
                }
            }
        } else {
            std::cout << "Other event type: " << event->type << "\n";
        }
        data_tail += event->size;
    }

    header->data_tail = data_tail;
    asm volatile("" ::: "memory");
}
