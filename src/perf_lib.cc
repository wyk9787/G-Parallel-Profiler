#include "perf_lib.hh"

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "log.h"

namespace {
// constants for attributes
constexpr auto SAMPLE_PERIOD = 10000;
constexpr auto SAMPLE_TYPE =
    PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;
constexpr auto NUM_DATA_PAGES = 256;
constexpr auto PAGE_SIZE = 0x1000LL;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
}  // namespace

SampleRecord *PerfLib::GetNextRecord() {
  if (!HasNextRecord()) return nullptr;
  perf_event_header *event_header = reinterpret_cast<perf_event_header *>(
      reinterpret_cast<uintptr_t>(data_) +
      mmap_header_->data_tail % mmap_header_->data_size);

  void *event_data = reinterpret_cast<void *>(
      reinterpret_cast<uintptr_t>(event_header) + sizeof(perf_event_header));

  // Advance our tail pointer manually
  // Kernel will update this for us
  mmap_header_->data_tail += event_header->size;
  if (event_header->type == PERF_RECORD_SAMPLE) {
    SampleRecord *result = reinterpret_cast<SampleRecord *>(event_data);
    return result;
  }
  if (event_header->type == PERF_RECORD_FORK) {
    INFO << "Find out fork";
    ForkRecord *fork_record = reinterpret_cast<ForkRecord *>(event_data);
    INFO << "pid: " << fork_record->pid;
    INFO << "ppid: " << fork_record->ppid;
    INFO << "tid: " << fork_record->tid;
    INFO << "ptid: " << fork_record->ptid;
    INFO << "time: " << fork_record->time;
  }
  return nullptr;
}

void PerfLib::SetupRingBuffer() {
  // the mmap size has to be 1 + 2^n pages, where the first page is a metadata
  // page
  uint64_t buffer_size = (1 + NUM_DATA_PAGES) * PAGE_SIZE;
  void *buffer =
      mmap(/*addr=*/NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
           /*offset=*/0);
  REQUIRE(buffer != MAP_FAILED) << "mmap failed: " << strerror(errno);

  mmap_header_ = reinterpret_cast<perf_event_mmap_page *>(buffer);
  data_ = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(mmap_header_) +
                                   PAGE_SIZE);
}

int PerfLib::PerfEventOpen(pid_t child_pid) {
  struct perf_event_attr pe = {
      .type = PERF_TYPE_HARDWARE,  // Count occurrences of a hardware event
      .size = sizeof(struct perf_event_attr),
      .config = PERF_COUNT_HW_REF_CPU_CYCLES,  // Count cycles on the CPU
                                               // independent of frequency
                                               // scaling
      .sample_period = SAMPLE_PERIOD,          // period of sampling
      .sample_type = SAMPLE_TYPE,              // types of sample we collect
      .disabled = 1,  // Start the counter in a disabled state
      .inherit = 0,   // Processes or threads created in the child should
                      // also be profiled
      .task = 1,
      .exclude_kernel = 1,  // Do not take samples in the kernel
      .exclude_hv = 1,      // Do not take samples in the hypervisor
  };

  fd_ = perf_event_open(&pe, child_pid, /*cpu=*/-1, /*group_fd=*/-1,
                        /*flags=*/0);
  REQUIRE(fd_ != -1) << "perf_event_open failed: " << strerror(errno);
  SetupRingBuffer();
  return fd_;
}

