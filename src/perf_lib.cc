#include "perf_lib.hh"

#include <string.h>
#include <sys/mman.h>

#include "log.h"

namespace {
long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu,
                     int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
}  // namespace

void *PerfLib::GetNextRecord(int *type) {
  perf_event_header *event_header = reinterpret_cast<perf_event_header *>(
      reinterpret_cast<uintptr_t>(data_) +
      mmap_header_->data_tail % mmap_header_->data_size);

  void *event_data = reinterpret_cast<void *>(
      reinterpret_cast<uintptr_t>(event_header) + sizeof(perf_event_header));
  *type = event_header->type;

  // Advance our tail pointer manually
  // Kernel will update this for us
  mmap_header_->data_tail += event_header->size;
  return event_data;
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
      .disabled = 1,        // Start the counter in a disabled state
      .inherit = 0,         // Processes or threads created in the child should
                            // also be profiled
      .task = 1,            // enable fork/exit record
      .exclude_kernel = 1,  // Do not take samples in the kernel
      .exclude_callchain_kernel = 1,
      .exclude_hv = 1,  // Do not take samples in the hypervisor
      .watermark = 1,   // set up to actually receive overflow notification
      .wakeup_watermark =
          1,  // receive overflow notification for all PERF_RECORD types
  };

  fd_ = perf_event_open(&pe, child_pid, /*cpu=*/-1, /*group_fd=*/-1,
                        /*flags=*/0);

  // We may have missed a thread entirely
  if (fd_ == -1 && errno == ESRCH) {
    return -1;
  } else {
    REQUIRE(fd_ != -1) << "perf_event_open failed: " << strerror(errno);
  }
  SetupRingBuffer();
  return fd_;
}

