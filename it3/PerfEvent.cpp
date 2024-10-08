#include "PerfEvent.h"
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>


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
    pe.mmap = 1; // To tracl MMAP events
    pe.comm = 1;

    fd = perf_event_open(&pe, pid, -1, -1, 0);
    if (fd == -1) {
    	if (errno == ESRCH) {
		std::cerr << "Process is too fast. Unable to attach to PID " << pid << ".\n";
    	} else {
		error_and_exit("perf_event_open");
        }
        fd = -1;
        return;
    }

    if (is_sampling) {
        mmap_buffer = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mmap_buffer == MAP_FAILED) {
            error_and_exit("mmap");
        }
    }

    if (fd != -1) {
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    }
}

// Destructor for PerfEvent
PerfEvent::~PerfEvent() {
    if (is_sampling && mmap_buffer) {
        munmap(mmap_buffer, BUFFER_SIZE);
    }
    if (fd != -1) {
	close(fd);
    }
}

// Read event count
void PerfEvent::read_count() {
    if (fd == -1) {
	return;
    }

    long long count;
    if (read(fd, &count, sizeof(long long)) == -1) {
        error_and_exit("read");
    }
    std::cout << "Event count (" << event_name << ") for PID " << pid << ": " << count << "\n\n";
}

// Read and process samples
void PerfEvent::read_samples(std::unordered_map<int, PerfEvent*> &events_map, std::unordered_map<std::string, int> &global_histogram, std::map<uint64_t, std::pair<uint64_t, std::string>> &global_mmap_records, std::unordered_map<uint64_t, int> &global_ip_histogram) {
    if (mmap_buffer == nullptr || fd == -1) {
	return;
    }

    struct perf_event_mmap_page *header = (struct perf_event_mmap_page *)mmap_buffer;
    char *data = (char *)mmap_buffer + header->data_offset;
    uint64_t data_head = header->data_head;
    asm volatile("" ::: "memory");
    uint64_t data_tail = header->data_tail;


    while (data_tail < data_head) {
        struct perf_event_header *event = (struct perf_event_header *)(data + (data_tail & (BUFFER_SIZE - 1)));
	if (event == nullptr) return;
        if (event->type == PERF_RECORD_SAMPLE) {
            // The sample contains the IP and TID (pid/tid) data
            uint64_t ip;
            memcpy(&ip, (char *)event + sizeof(struct perf_event_header), sizeof(uint64_t));
            uint32_t pid, tid;
            memcpy(&pid, (char *)event + sizeof(struct perf_event_header) + sizeof(uint64_t), sizeof(uint32_t));
            memcpy(&tid, (char *)event + sizeof(struct perf_event_header) + sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint32_t));
            // std::cout << "IP: " << std::hex << ip << std::dec << " PID: " << pid << "\n";

            uint64_t *sample = (uint64_t *)((uint8_t *)event + sizeof(struct perf_event_header));
            ip = *sample;

	    // Updating global ip hist
            global_ip_histogram[ip]++;

            // Updating global lib hist
            for (const auto& [start_addr, info] : global_mmap_records) {
                const auto& [end_addr, filename] = info;
                if (ip >= start_addr && ip < end_addr) {
                    global_histogram[filename]++;
                    break;
                }
            }

        } else if (event->type == PERF_RECORD_FORK) {
            struct { uint32_t pid, ppid, tid, ptid; } fork;
            memcpy(&fork, (char *)event + sizeof(struct perf_event_header), sizeof(fork));
            std::cout << "FORK event: PID " << fork.pid << " PPID " << fork.ppid << "\n";

            // Create a new PerfEvent for the forked process
            PerfEvent* new_event = new PerfEvent(event_name, is_sampling, fork.pid, sample_period);
            if (new_event->fd != -1) {
		events_map[new_event->fd] = new_event;
            } else {
		delete new_event;
            }
        } else if (event->type == PERF_RECORD_MMAP) {
            struct {
                struct perf_event_header header;
                uint32_t pid, tid;
                uint64_t addr, len, pgoff;
                char filename[256];
            } *mmap_event = (decltype(mmap_event)) event;

            uint64_t start_addr = mmap_event->addr;
            uint64_t end_addr = start_addr + mmap_event->len;

            // Updating mmap records
            global_mmap_records[start_addr] = {end_addr, mmap_event->filename};


            // Mmap info
            std::cout << "mmap event: pid=" << mmap_event->pid << ", tid=" << mmap_event->tid
                      << ", addr=" << mmap_event->addr << ", len=" << mmap_event->len
                      << ", pgoff=" << mmap_event->pgoff << ", filename=" << mmap_event->filename << std::endl; 
        } else if (event->type == PERF_RECORD_COMM) {
            struct {
                struct perf_event_header header;
                uint32_t pid, tid;
                char comm[16];
            } *comm_event = (decltype(comm_event)) event;

            // Print COMM event info
            std::cout << "COMM event: Process " << comm_event->pid 
                      << " changed name to " << comm_event->comm << "\n";
        } else {
            // std::cout << "Other event type: " << event->type << "\n";
            data_tail += event->size;
            break;
        }
	    data_tail += event->size;
    }

    header->data_tail = data_tail;
    asm volatile("" ::: "memory");
}
