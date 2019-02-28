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

// Mapping from tid to perf fd
std::unordered_map<pid_t, int> tid_to_fd;

// Used for epoll
int epoll_fd;

pid_t child_pid;

void DeleteFromEpoll(int fd) {
  INFO << "Deleting " << fd << " from epoll";
  epoll_event ev = {.events = EPOLLIN, {.fd = fd}};
  REQUIRE(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) != -1)
      << "epoll_ctl DEL failed: " << strerror(errno);
}

void AddToEpoll(int fd) {
  INFO << "Adding " << fd << " to epoll";
  epoll_event ev = {.events = EPOLLIN, {.fd = fd}};
  REQUIRE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != -1)
      << "epoll_ctl ADD failed: " << strerror(errno);
}

// Handle the record with corresponding fd
// Return true if the corresponding thread has exited
// Otherwise, return false
bool HandleRecord(int fd) {
  int type;
  void *event_data = perf_libs[fd].GetNextRecord(&type);

  // Check to if there is actually data available
  if (event_data == nullptr) {
    INFO << "NOT AVAILABLE";
    return false;
  }

  if (type == PERF_RECORD_SAMPLE) {
    SampleRecord *sample_record = reinterpret_cast<SampleRecord *>(event_data);
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
    p.StartSampling();

    // Update global bookkeeping
    tid_to_fd.insert({tid, perf_fd});
    AddToEpoll(perf_fd);
    perf_libs.insert({perf_fd, p});
  } else if (type == PERF_RECORD_EXIT) {
    // Parse tid out of data
    TaskRecord *exit_record = reinterpret_cast<TaskRecord *>(event_data);
    INFO << "Exit Reocrd = tid: " << exit_record->tid
         << ", ptid: " << exit_record->ptid;
    pid_t tid = exit_record->tid;
    int perf_fd = tid_to_fd[tid];

    // Update global bookkeeping
    tid_to_fd.erase(tid);
    DeleteFromEpoll(perf_fd);
    perf_libs.erase(perf_fd);
    if (tid == child_pid) {
      return true;
    }
  } else {
    INFO << "Found a missing record!";
  }
  return false;
}

void RunProfiler() {
  bool running = true;
  epoll_event ev_list[MAX_EPOLL_EVENTS];
  while (running) {
    int ready_num =
        epoll_wait(epoll_fd, ev_list, MAX_EPOLL_EVENTS, /*timeout=*/10000);
    REQUIRE(ready_num != -1) << "epoll_wait failed: " << strerror(errno);

    if (ready_num == 0) INFO << "TIMEOUT!!!";
    for (int i = 0; i < ready_num; ++i) {
      if (HandleRecord(ev_list[i].data.fd)) {
        running = false;
      }
    }
  }

  // Print the count of events from perf_event
  printf("\nProfiler Finished:\n");
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
    tid_to_fd.insert({child_pid, perf_fd});
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

