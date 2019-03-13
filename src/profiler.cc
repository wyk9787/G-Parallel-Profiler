#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

#include "inspect.h"
#include "log.h"
#include "perf_lib.hh"

#define MAX_EPOLL_EVENTS 10

// Type alias for a mapping from function name to number of times occured
using function_freq_t = std::unordered_map<std::string, size_t>;

// Mapping from thread id to function frequency table
std::unordered_map<pid_t, function_freq_t> thread_mapping;

// Mapping from thread id to the total number of samples collected in this
// thread
std::unordered_map<pid_t, size_t> thread_sample_count;

// Mapping from perf fd to PerfLib
std::unordered_map<int, PerfLib> perf_libs;

// Used for epoll
int epoll_fd;

// Main child pid
pid_t child_pid;

// Total number of samples collected so far
static size_t sample_count = 0;

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

      // NOTE: Only happen when sample period is not large enough (<= 1000000)
      if (tid == 0 || tid > 100000) {
        INFO << "Found a wierd record: tid = " << tid << ", ip = " << ip;
      }

      std::string function_name = "";
      const char *ret = address_to_function(tid, ip);
      if (ret == NULL) {
        function_name = "somewhere";
        /* INFO << "RETURN NULL!!!: " << ip << ", " << tid; */
      } else {
        function_name = std::string(ret);
      }

      // Update bookkeeping data structures
      thread_mapping[tid][function_name]++;
      thread_sample_count[tid]++;
      sample_count++;

      // TODO: Integrates callchain
      /* uint64_t num_funcs = sample_record->nr; */
      /* uint64_t *ips = reinterpret_cast<uint64_t *>(&(sample_record->ips)); */
      /* for (size_t i = 0; i < num_funcs; ++i) { */
      /*   void *cur_ip = reinterpret_cast<void *>(ips[i]); */
      /*   const char *ret = address_to_function(tid, cur_ip); */
      /*   if (ret == NULL) { */
      /*     function_name = "callchain somewhere"; */
      /*   } else { */
      /*     function_name = std::string(ret); */
      /*     INFO << function_name; */
      /*   } */
      /* } */
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

      // Check to see if the main child has exited or not
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
  printf("\nProfiler Output:\n");

  // Loop through every thread
  for (auto p : thread_mapping) {
    std::cout << "  Thread " << p.first << ":" << std::endl;
    size_t total_count = thread_sample_count[p.first];

    // Sort the function-to-count mapping by its value (count)
    // Since the two functions can possibly have the same sample count (very
    // unlikely in large programs), we use multimap instead of map here
    std::multimap<size_t, std::string, std::greater<size_t>> dst;
    std::transform(
        p.second.begin(), p.second.end(), std::inserter(dst, dst.begin()),
        [](const std::pair<std::string, size_t> &tmp) {
          return std::pair<size_t, std::string>(tmp.second, tmp.first);
        });

    for (const auto &q : dst) {
      std::cout << "    " << q.second << ": " << q.first * SAMPLE_PERIOD
                << " cycles "
                << static_cast<double>(q.first) / total_count * 100 << "%"
                << std::endl;
    }
  }
  std::cout << "Total: " << sample_count * SAMPLE_PERIOD << " cycles"
            << std::endl;
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

