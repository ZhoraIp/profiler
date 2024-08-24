#include <iostream>
#include <sys/ioctl.h>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include "PerfEvent.h"
#include "utils.h"
#include <map>
#include <unordered_map>


std::unordered_map<std::string, int> global_histogram;

std::map<uint64_t, std::pair<uint64_t, std::string>> global_mmap_records;

std::unordered_map<uint64_t, int> global_ip_histogram;

void print_global_histogram() {
    std::cout << "Global histogram of frequently visited code sections and modules:\n";
    for (const auto& entry : global_histogram) {
        std::cout << "Module: " << entry.first << ", Hits: " << entry.second << "\n";
    }

    std::cout << "\nGlobal histogram of frequently visited IP addresses:\n";
    for (const auto& entry : global_ip_histogram) {
        std::cout << "Address: 0x" << std::hex << entry.first << std::dec << ", Hits: " << entry.second << "\n";
    }
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " [-time <time>] [-count <event>] [-record <event:period>] command arg1 arg2 ...\n";
        return 1;
    }

    int sleep_time = 0;
    std::string count_event;
    std::string record_event;
    uint64_t sample_period = 0;
    std::vector<std::string> program_args;
    bool time_set = false;
    bool count_set = false;
    bool record_set = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-time") == 0 && i + 1 < argc) {
            sleep_time = std::atoi(argv[++i]);
            if (sleep_time <= 0) {
                std::cerr << "Invalid time value.\n";
                return 1;
            }
            time_set = true;
        } else if (strcmp(argv[i], "-count") == 0 && i + 1 < argc) {
            count_event = argv[++i];
            count_set = true;
        } else if (strcmp(argv[i], "-record") == 0 && i + 1 < argc) {
            std::string record_arg = argv[++i];
            size_t colon_pos = record_arg.find(':');
            if (colon_pos == std::string::npos) {
                std::cerr << "Invalid record format.\n";
                return 1;
            }
            record_event = record_arg.substr(0, colon_pos);
            sample_period = std::stoull(record_arg.substr(colon_pos + 1));
            if (sample_period == 0) {
                std::cerr << "Invalid sample period.\n";
                return 1;
            }
            record_set = true;
        } else {
            program_args.push_back(argv[i]);
        }
    }

    if (program_args.empty()) {
        std::cerr << "Usage: " << argv[0] << " [-time <time>] [-count <event>] [-record <event:period>] command arg1 arg2 ...\n";
        return 1;
    }

    // Prepare arguments for execvp
    std::vector<char*> exec_args;
    for (auto &arg : program_args) {
        exec_args.push_back(&arg[0]);
    }
    exec_args.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        error_and_exit("pipe");
    }

    pid_t pid = fork();
    if (pid == -1) {
        error_and_exit("fork");
    }

    if (pid == 0) {  // Child process
        close(pipefd[1]);

        // Waiting for the signal from the parent
        char buffer;
        if (read(pipefd[0], &buffer, 1) != 1) {
            error_and_exit("read");
        }
        close(pipefd[0]);

        // Executing the program
        execvp(exec_args[0], exec_args.data());
        error_and_exit("execvp");
    } else {  // Parent process
        close(pipefd[0]);

        PerfEvent *count_event_perf = nullptr;
        PerfEvent *record_event_perf = nullptr;
        std::unordered_map<int, PerfEvent*> events_map;

        if (count_set) {
            count_event_perf = new PerfEvent(count_event, false, pid);
        }

        if (record_set) {
            record_event_perf = new PerfEvent(record_event, true, pid, sample_period);
            events_map[record_event_perf->fd] = record_event_perf;
        }

        auto begin = std::chrono::steady_clock::now();

        if (time_set) {
            sleep(sleep_time);
        }

        // Sending signal to the child
        if (write(pipefd[1], "", 1) != 1) {
            error_and_exit("write");
        }
        close(pipefd[1]);


        while (!events_map.empty()) {
            std::vector<struct pollfd> poll_fds(events_map.size());
            int i = 0;
            for (const auto& pair : events_map) {
                poll_fds[i].fd = pair.first;
                poll_fds[i].events = POLLIN;
                ++i;
            }


	    // Poll for events
            int poll_result = poll(poll_fds.data(), poll_fds.size(), -1);
            if (poll_result == -1) {
                error_and_exit("poll");
            }

            for (const auto& pfd : poll_fds) {
                if (pfd.revents & POLLIN) {
                    if (events_map[pfd.fd] != nullptr) {
			events_map[pfd.fd]->read_samples(events_map, global_histogram, global_mmap_records, global_ip_histogram);
                    }
                }
                if (pfd.revents & POLLHUP) {
                    close(pfd.fd);
		    delete events_map[pfd.fd];
                    events_map.erase(pfd.fd);
                }
            }

        }

        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

        std::cout << "Elapsed time: " << elapsed_ms.count() << " milliseconds\n";

        // Read final counts and clean up
        if (count_set) {
            ioctl(count_event_perf->fd, PERF_EVENT_IOC_DISABLE, 0);
            count_event_perf->read_count();
            delete count_event_perf;
        }

        for (auto& pair : events_map) {
            ioctl(pair.second->fd, PERF_EVENT_IOC_DISABLE, 0);
            delete pair.second;
        }
    }

    print_global_histogram();

    return 0;
}

