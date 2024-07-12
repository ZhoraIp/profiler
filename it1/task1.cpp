#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <set>
#include <vector>

#define MAX_PROCESSES 100

// Partial geometric mean log
double geometric_mean_log_divisors(int n) {
	int count = 0;
    double result = 1;
	std::set <int> deviders;

	for (int i = 2; i <= n; ++i) {
		for (int j = 2; j <= sqrt(i); ++j) {
			if (i % j == 0) {
				deviders.insert(j);
				deviders.insert(i / j);
			}
		}
	}

	count = deviders.size();
	if (count == 0) {
		return 1;
	}

	for (int n : deviders) {
		result *= pow(log(n), (double) 1 / count );
	}

    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <N> <M> <k>\n", argv[0]);
        return 1;
    }

    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    int k = atoi(argv[3]);

    if (N <= 0 || M <= 0 || k <= 0 || k > MAX_PROCESSES) {
        fprintf(stderr, "Invalid input values. Max processes: %d\n", MAX_PROCESSES);
        return 1;
    }

    int pipefd[k][2];
    pid_t pids[k];
    struct pollfd fds[k];


    // Creation of pipes and processes
    for (int i = 0; i < k; ++i) {
        if (pipe(pipefd[i]) == -1) {
            perror("pipe");
            return 1;
        }

        pids[i] = fork();

        if (pids[i] == -1) {
            perror("fork");
            return 1;
        }

        if (pids[i] == 0) { // Child process
        	printf("Process %d is open\n", i);

            close(pipefd[i][0]); // Closing file descriptor for reading

			srand(getpid());
            int random_limit = rand() % M + 1;
            double result = 0.0;

			// Writing a random limit
			if (write(pipefd[i][1], &random_limit, sizeof(random_limit)) != sizeof(random_limit)) {
				perror("write");
				close(pipefd[i][1]);
				return 1;
			}

			// Calculating the average and writing the results into pipe
            for (int j = 1; j <= random_limit; ++j) {
                result = geometric_mean_log_divisors(N * j + i);

				//printf("Process %d partial result is %lf\n", i, result);
				if (write(pipefd[i][1], &result, sizeof(result)) != sizeof(result)) {
					perror("write");
					close(pipefd[i][1]);
					return 1;
				}

            }

            close(pipefd[i][1]);

            return 0;
        } else { // Parent process
            close(pipefd[i][1]); // Closing file descriptor for writing

            fds[i].fd = pipefd[i][0];
            fds[i].events = POLLIN;
        }
    }

    double total_result = 1;
    int count = 0;
    int active_processes = k;

	std::vector <bool> entry(k, 0);

	// Poll
    while (active_processes > 0) {
        int poll_count = poll(fds, k, -1);

        if (poll_count == -1) {
            perror("poll");
            return 1;
        }

        for (int i = 0; i < k; ++i) {
            if (fds[i].revents & POLLIN) {
				int limit;
				double result;

				// Checking the first read
            	if (!entry[i]) {
            		entry[i] = 1;
					ssize_t s = read(fds[i].fd, &limit, sizeof(limit));

				    if (s == -1) {
	                    perror("read");
		                return 1;
					} else {
						printf("Process %d random number is %d\n", i, limit);
					}
				}
				if (entry[i]) {
					ssize_t s = 1;
					while (s > 0) {
						s = read(fds[i].fd, &result, sizeof(result));
						if (s == -1) {
							perror("read");
							return 1;
						} else if (s != 0) { // Recalculation of the mean
							printf("Process %d partial result is %lf\n", i, result);
							total_result = pow(total_result, (double) count / (count + 1)) * pow(result, (double) 1 / (count + 1));
							count++;
						}
					}
				}
			}

			if (fds[i].revents & POLLHUP) {
				close(fds[i].fd);
				fds[i].fd = -1;
				active_processes--;
			}
        }
    }
    printf("Total geometric mean log result: %lf\n", total_result);

    // Waiting for child processes
    for (int i = 0; i < k; ++i) {
        int status;
        waitpid(pids[i], &status, 0);
    }

    return 0;
}

