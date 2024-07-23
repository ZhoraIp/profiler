#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <errno.h>

void error_and_exit(const std::string &msg) {
    perror(msg.c_str());
    exit(EXIT_FAILURE);
}

// perf_event_open wrapper
int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " [-time <time>] [-count <event>] command arg1 arg2 ...\n";
        return 1;
    }

    // Parsing of the sleep time and count event
    int sleep_time = 0;
    std::string count_event;
    std::vector<std::string> program_args;
    bool time_set = false;
    bool count_set = false;

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
        } else {
            program_args.push_back(argv[i]);
        }
    }

    if (program_args.empty()) {
        std::cerr << "Usage: " << argv[0] << " [-time <time>] [-count <event>] command arg1 arg2 ...\n";
        return 1;
    }

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

        // Executing of the program
        execvp(exec_args[0], exec_args.data());
        error_and_exit("execvp");
    } else {  // Parent process
        close(pipefd[0]);

        int fd = -1;
        if (count_set) {
            // Perf_event_attr set up
            struct perf_event_attr pe;
            memset(&pe, 0, sizeof(struct perf_event_attr));
            pe.type = PERF_TYPE_HARDWARE;
            pe.size = sizeof(struct perf_event_attr);

            if (count_event == "instructions") {
                pe.config = PERF_COUNT_HW_INSTRUCTIONS;
            } else if (count_event == "cycles") {
                pe.config = PERF_COUNT_HW_CPU_CYCLES;
            } else if (count_event == "cache-misses") {
                pe.config = PERF_COUNT_HW_CACHE_MISSES;
            } else {
                std::cerr << "Unsupported event type.\n Supported events:\n 1)Instructions\n 2)Cycles\n 3)Cache-misses\n";
                return 1;
            }

            pe.disabled = 1;
            pe.exclude_kernel = 1;
            pe.exclude_hv = 1;

            fd = perf_event_open(&pe, pid, -1, -1, 0);
            if (fd == -1) {
                error_and_exit("perf_event_open");
            }
        }

        // Start time
        auto begin = std::chrono::steady_clock::now();

        if (time_set) {
            sleep(sleep_time);
        }

        if (count_set) {
            ioctl(fd, PERF_EVENT_IOC_RESET, 0);
            ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
        }

        // Sending signal to the child
        if (write(pipefd[1], "", 1) != 1) {
            error_and_exit("write");
        }
        close(pipefd[1]);

        int status;
        waitpid(pid, &status, 0);

        if (count_set) {
            ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
        }

        // End time
        auto end = std::chrono::steady_clock::now();

        // Calculation of the elapsed time
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

        std::cout << "Elapsed time: " << elapsed_ms.count() << " milliseconds\n";

        if (count_set) {
            // Reading the number of events
            long long count;
            if (read(fd, &count, sizeof(long long)) == -1) {
                error_and_exit("read");
            }
            close(fd);

            std::cout << "Event count (" << count_event << "): " << count << "\n";
        }
    }

    return 0;
}

