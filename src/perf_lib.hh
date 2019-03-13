#ifndef PERF_LIB_HH
#define PERF_LIB_HH

#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

// Memory mapping for PERF_SAMPLE_RECORD
struct SampleRecord {
  uint64_t ip;    // instruction pointer
  uint32_t pid;   // process id
  uint32_t tid;   // thread id
  uint64_t nr;    // number of functions in challchains
  uint64_t *ips;  // array of instruction pointers in callchains
};

// Memory mapping for PERF_FORK_RECORD and PERF_EXIT_RECORD
struct TaskRecord {
  uint32_t pid;
  uint32_t ppid;
  uint32_t tid;
  uint32_t ptid;
  uint64_t time;
};

// constants for attributes
constexpr auto SAMPLE_PERIOD = 10000000;
constexpr auto SAMPLE_TYPE =
    PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;
constexpr auto NUM_DATA_PAGES = 256;
constexpr auto PAGE_SIZE = 0x1000LL;

// A wrapper library around perf_event_open to sample and get records
class PerfLib {
 public:
  int PerfEventOpen(pid_t child_pid);

  // Return next record and store the type of the record in type
  void *GetNextRecord(int *type);

  // stop, resume, or reset sampling
  void StartSampling() {
    REQUIRE(ioctl(fd_, PERF_EVENT_IOC_ENABLE, 1) != -1)
        << "Failed to start perf event: " << strerror(errno) << " (" << fd_
        << ")";
  }

  void StopSampling() {
    REQUIRE(ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0) != -1)
        << "Failed to stop perf event: " << strerror(errno) << " (" << fd_
        << ")";
  }

  void ResetSampling() {
    REQUIRE(ioctl(fd_, PERF_EVENT_IOC_RESET, 0) != -1)
        << "Failed to reset perf event: " << strerror(errno) << " (" << fd_
        << ")";
  }

  // Decide if next record exists or not
  bool HasNextRecord() {
    return mmap_header_->data_head != mmap_header_->data_tail;
  }

 private:
  // Setup the mmap ring buffer
  void SetupRingBuffer();

  int fd_;                             // fd associated with this perf call
  perf_event_mmap_page *mmap_header_;  // header section for mmap region
  void *data_;                         // data section for mmap region
};

#endif  // PERF_LIB_HH
