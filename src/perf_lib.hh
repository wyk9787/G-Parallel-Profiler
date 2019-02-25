#ifndef PERF_LIB_HH
#define PERF_LIB_HH

#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "log.h"

struct SampleRecord {
  uint64_t ip;    // instruction pointer
  uint32_t tid;   // thread id
  uint64_t nr;    // number of functions in challchains
  uint64_t *ips;  // array of instruction pointers in callchains
};

class PerfLib {
 public:
  void PerfEventOpen(pid_t child_pid);
  SampleRecord *GetNextRecord();
  inline bool HasNextRecord();
  inline void StartSampling();
  inline void StopSampling();
  inline void ResetSampling();

 private:
  struct perf_event_attr SetupAttribute();
  void SetupRingBuffer();

  int fd_;
  perf_event_mmap_page *header_;
  void *data_;
};

#endif  // PERF_LIB_HH
