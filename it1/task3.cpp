#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>

void error_and_exit(const std::string &msg) {
    perror(msg.c_str());
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [-time <time>] command arg1 arg2 ... \n", argv[0]);
        return 1;
    }

	// Parsing of the sleep time
    int sleep_time = 0;
    std::vector<std::string> program_args;
    bool time_set = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-time") == 0 && i + 1 < argc) {
            sleep_time = std::atoi(argv[++i]);
            if (sleep_time <= 0) {
                fprintf(stderr, "Invalid time value.\n");
                return 1;
            }
            time_set = true;
        } else {
            program_args.push_back(argv[i]);
        }
    }

    if (!time_set || program_args.empty()) {
        fprintf(stderr, "Usage: %s [-time <time>] command arg1 arg2 ... \n", argv[0]);
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

        // Execute the program
        execvp(exec_args[0], exec_args.data());
        error_and_exit("execvp");
    } else {  // Parent process
        close(pipefd[0]);

		//  Start time
		auto begin = std::chrono::steady_clock::now();

        sleep(sleep_time);

        // Sending signal to the child
        if (write(pipefd[1], "", 1) != 1) {
            error_and_exit("write");
        }
        close(pipefd[1]);

        int status;
        waitpid(pid, &status, 0);

		// End time
		auto end = std::chrono::steady_clock::now();

		// Calculation of the elapsed time
		auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);


		printf("Elapsed time: %d milliseconds\n", elapsed_ms);
    }

    return 0;
}

