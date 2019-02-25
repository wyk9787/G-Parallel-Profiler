#include "perf_lib.hh"

#include <string.h>
#include <sys/mman.h>

#include "log.h"

namespace {
// constants for attributes
constexpr auto SAMPLE_PERIOD = 100000;
constexpr auto SAMPLE_TYPE =
    PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;
constexpr auto NUM_DATA_PAGES = 256;
constexpr auto PAGE_SIZE = 1000LL;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
}  // namespace

SampleRecord *PerfLib::GetNextRecord() {
  perf_event_header *event_header = reinterpret_cast<perf_event_header *>(
      reinterpret_cast<uintptr_t>(data_) +
      header_->data_tail % header_->data_size);

  void *event_data = reinterpret_cast<void *>(
      reinterpret_cast<uintptr_t>(event_header) + sizeof(perf_event_header));

  // Advance our tail pointer manually
  // Kernel will update this for us
  header_->data_tail += event_header->size;
  SampleRecord *result = reinterpret_cast<SampleRecord *>(event_data);
  return result;
}

// Decide if next record exists or not
bool PerfLib::HasNextRecord() {
  return header_->data_head != header_->data_tail;
}

// stop, resume, or reset sampling
void PerfLib::StartSampling() {
  REQUIRE(ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0) != -1)
      << "Failed to start perf event: " << strerror(errno) << " (" << fd_
      << ")";
}

void PerfLib::StopSampling() {
  REQUIRE(ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0) != -1)
      << "Failed to stop perf event: " << strerror(errno) << " (" << fd_ << ")";
}

void PerfLib::ResetSampling() {
  REQUIRE(ioctl(fd_, PERF_EVENT_IOC_RESET, 0) != -1)
      << "Failed to reset perf event: " << strerror(errno) << " (" << fd_
      << ")";
}

struct perf_event_attr PerfLib::SetupAttribute() {
  struct perf_event_attr pe = {
      .type = PERF_TYPE_HARDWARE,  // Count occurrences of a hardware event
      .size = sizeof(struct perf_event_attr),
      .config = PERF_COUNT_HW_REF_CPU_CYCLES,  // Count cycles on the CPU
                                               // independent of frequency
                                               // scaling
      .sample_period = SAMPLE_PERIOD,          // period of sampling
      .sample_type = SAMPLE_TYPE,              // types of sample we collect
      .disabled = 1,        // Start the counter in a disabled state
      .inherit = 1,         // Processes or threads created in the child should
                            // also be profiled
      .exclude_kernel = 1,  // Do not take samples in the kernel
      .exclude_hv = 1,      // Do not take samples in the hypervisor
      .enable_on_exec = 1   // Enable profiling on the first exec call
  };
  return pe;
}

void PerfLib::PerfEventOpen(pid_t child_pid) {
  struct perf_event_attr pe = SetupAttribute();
  fd_ =
      perf_event_open(&pe, child_pid, /*cpu=*/-1, /*group_fd=*/-1, /*flags=*/0);
  REQUIRE(fd_ != -1) << "perf_event_open failed: " << strerror(errno);
}

void PerfLib::SetupRingBuffer() {
  // the mmap size has to be 1 + 2^n pages, where the first page is a metadata
  // page
  uint64_t buffer_size = (1 + NUM_DATA_PAGES) * PAGE_SIZE;
  void *buffer =
      mmap(/*addr=*/0, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
           /*offset=*/0);
  REQUIRE(buffer != MAP_FAILED) << "mmap failed: " << strerror(errno);

  header_ = reinterpret_cast<perf_event_mmap_page *>(buffer);
  data_ = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(header_) +
                                   PAGE_SIZE);
}
