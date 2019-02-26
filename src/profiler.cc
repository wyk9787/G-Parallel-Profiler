#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

#include "log.h"
#include "perf_lib.hh"

// Type alias for a mapping from function name to number of times occured
using function_freq_t = std::unordered_map<std::string, size_t>;

// Mapping from thread id to function frequency table
std::unordered_map<pid_t, function_freq_t> thread_mapping;

void run_profiler(pid_t child_pid) {
  PerfLib perf_lib;
  int perf_fd = perf_lib.PerfEventOpen(child_pid);

  int epoll_fd = epoll_create1(/*flags=*/0);
  REQUIRE(epoll_fd != -1) << "epoll_create1 failed: " << strerror(errno);

  epoll_event ev;
  ev.events = EPOLLIN;
  REQUIRE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, perf_fd, &ev) != -1)
      << "epoll_ct ADD failed: " << strerror(errno);

  perf_lib.StartSampling();
  bool running = true;
  epoll_event rev;
  while (running) {
    REQUIRE(epoll_wait(epoll_fd, &rev, /*maxevents=*/10, 10) != -1)
        << "epoll_wait failed: " << strerror(errno);

    SampleRecord *record = perf_lib.GetNextRecord();

    // If the record exists and is indeed a record we want
    if (record != nullptr) {
      void *ip = reinterpret_cast<void *>(record->ip);
      pid_t tid = static_cast<pid_t>(record->tid);
      INFO << "ip: " << ip << ", tid: " << tid;
    }

    // Check to see if the child has exited or not
    int status;
    waitpid(child_pid, &status, WNOHANG);
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      running = false;
    }
  }

  // Print the count of events from perf_event
  printf("\nProfiler Output:\n");

  long long count;
  REQUIRE(read(perf_fd, &count, sizeof(long long)) == sizeof(long long))
      << "read failed: " << strerror(errno);
  printf("  ref cycles: %lld\n", count);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s <command to run with profiler> [command arguments...]\n",
            argv[0]);
    exit(1);
  }

  // Create a pipe so the parent can tell the child to exec
  int pipefd[2];
  REQUIRE(pipe(pipefd) == 0) << "pipe failed: " << strerror(errno);

  // Create a child process
  pid_t child_pid = fork();
  REQUIRE(child_pid != -1) << "fork failed: " << strerror(errno);

  if (child_pid == 0) {
    // In child process. Read from the pipe to pause until the parent resumes
    // the child.
    char c;
    REQUIRE(read(pipefd[0], &c, 1) == 1) << "read failed: " << strerror(errno);
    close(pipefd[0]);
    close(pipefd[1]);

    REQUIRE(execvp(argv[1], &argv[1])) << "execvp failed: " << strerror(errno);
  } else {
    // In the parent process

    // Set up perf_event for the child process

    // Wake up the child process by writing to the pipe
    char c = 'A';
    REQUIRE(write(pipefd[1], &c, 1) == 1)
        << "write failed: " << strerror(errno);
    close(pipefd[0]);
    close(pipefd[1]);

    // Start profiling
    run_profiler(child_pid);
  }

  return 0;
}

