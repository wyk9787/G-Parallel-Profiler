#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "perf_lib.hh"

#define MAX_EPOLL_EVENTS 10

// Type alias for a mapping from function name to number of times occured
using function_freq_t = std::unordered_map<std::string, size_t>;

// Mapping from thread id to function frequency table
std::unordered_map<pid_t, function_freq_t> thread_mapping;

// Mapping from perf fd to PerfLib
std::unordered_map<int, PerfLib> perf_libs;

// Used for epoll
int epoll_fd;

pid_t child_pid;

void DeleteFromEpoll(int fd) {
  epoll_event ev = {.events = EPOLLIN, {.fd = fd}};
  REQUIRE(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) != -1)
      << "epoll_ctl DEL failed: " << strerror(errno);
}

void AddToEpoll(int fd) {
  epoll_event ev = {.events = EPOLLIN, {.fd = fd}};
  REQUIRE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != -1)
      << "epoll_ctl ADD failed: " << strerror(errno);
}

// Handle the record with corresponding fd
// Return true if the corresponding thread has exited
// Otherwise, return false
bool HandleRecord(int fd) {
  bool main_child_exited = false;
  bool has_exited = false;
  int type;
  while (perf_libs[fd].HasNextRecord()) {
    void *event_data = perf_libs[fd].GetNextRecord(&type);

    if (type == PERF_RECORD_SAMPLE) {
      SampleRecord *sample_record =
          reinterpret_cast<SampleRecord *>(event_data);
      void *ip = reinterpret_cast<void *>(sample_record->ip);
      pid_t tid = static_cast<pid_t>(sample_record->tid);
      INFO << "Sample Record = ip: " << ip << ", tid: " << tid;
    } else if (type == PERF_RECORD_FORK) {
      // Parse tid out of data
      TaskRecord *fork_record = reinterpret_cast<TaskRecord *>(event_data);
      INFO << "Fork Reocrd = tid: " << fork_record->tid
           << ", ppid: " << fork_record->ppid;
      pid_t tid = fork_record->tid;

      // Start perf_event_open
      PerfLib p;
      int perf_fd = p.PerfEventOpen(tid);

      // We missed the thread entirely
      if (perf_fd == -1) {
        INFO << "We missed the thread " << tid;
        // Do not start sampling since perf_event_open failed
        continue;
      }
      p.StartSampling();

      // Update global bookkeeping
      AddToEpoll(perf_fd);
      perf_libs.insert({perf_fd, p});
    } else if (type == PERF_RECORD_EXIT) {
      has_exited = true;

      // Parse tid out of data
      TaskRecord *exit_record = reinterpret_cast<TaskRecord *>(event_data);
      INFO << "Exit Reocrd = tid: " << exit_record->tid
           << ", ptid: " << exit_record->ptid;
      pid_t tid = exit_record->tid;

      // Update global bookkeeping
      DeleteFromEpoll(fd);
      if (tid == child_pid) {
        main_child_exited = true;
      }
    } else {
      INFO << "Found a missing record!";
    }
  }
  if (has_exited) {
    perf_libs.erase(fd);
  }
  return main_child_exited;
}

void RunProfiler() {
  bool running = true;
  epoll_event ev_list[MAX_EPOLL_EVENTS];
  while (running) {
    memset(ev_list, 0, sizeof(epoll_event) * MAX_EPOLL_EVENTS);
    int ready_num =
        epoll_wait(epoll_fd, ev_list, MAX_EPOLL_EVENTS, /*timeout=*/-1);
    REQUIRE(ready_num != -1) << "epoll_wait failed: " << strerror(errno);
    /* INFO << "ready_num: " << ready_num; */

    for (int i = 0; i < ready_num; ++i) {
      if (HandleRecord(ev_list[i].data.fd)) {
        running = false;
      }
      /* running = !HandleRecord(0); */
    }
  }

  // Print the count of events from perf_event
  printf("\nProfiler Finished\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s <command to run with profiler> [command arguments...]\n",
            argv[0]);
    exit(1);
  }

  // Initialize epoll
  epoll_fd = epoll_create1(/*flags=*/0);
  REQUIRE(epoll_fd != -1) << "epoll_create1 failed: " << strerror(errno);

  // Create a pipe so the parent can tell the child to exec
  int pipefd[2];
  REQUIRE(pipe(pipefd) == 0) << "pipe failed: " << strerror(errno);

  // Create a child process
  child_pid = fork();
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
    PerfLib p;
    int perf_fd = p.PerfEventOpen(child_pid);
    AddToEpoll(perf_fd);
    perf_libs.insert({perf_fd, p});

    // Start sampling
    p.StartSampling();

    // Wake up the child process by writing to the pipe
    char c = 'A';
    REQUIRE(write(pipefd[1], &c, 1) == 1)
        << "write failed: " << strerror(errno);
    close(pipefd[0]);
    close(pipefd[1]);

    // Start profiling
    RunProfiler();
  }

  return 0;
}

