#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <chrono>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s command arg1 arg2 ... \n", argv[0]);
        return 1;
    }


    // Start time
	auto begin = std::chrono::steady_clock::now();

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) { // Child process
        execvp(argv[1], &argv[1]);
        perror("execvp");
        return 1;
    } else { // Parent process
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return 1;
        }

        // End time
        auto end = std::chrono::steady_clock::now();

        // Calculation of elapsed time
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

        printf("Elapsed time: %d milliseconds\n", elapsed_ms);

	}
    return 0;
}

