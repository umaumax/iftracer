thread_local int tls_init_trigger = 1;
class FastLoggerCaller {
 public:
  FastLoggerCaller(int dummy) {}
  // use blank destructor for supressing fast_logger_caller variable by -O3 optimization
  ~FastLoggerCaller() {}
};
namespace {
#if __linux__
__attribute__((init_priority(101)))
#endif
FastLoggerCaller fast_logger_caller(tls_init_trigger);
}  // namespace

#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

extern "C" {
void __cyg_profile_func_enter(void* func_address, void* call_site);
void __cyg_profile_func_exit(void* func_address, void* call_site);
}

namespace {
#if __APPLE__
pid_t gettid() {
  uint64_t tid64 = 0;
  pthread_threadid_np(nullptr, &tid64);
  return static_cast<pid_t>(tid64);
}
#elif __linux__
pid_t __attribute__((no_instrument_function)) gettid() {
  return syscall(SYS_gettid);
}
#else
#error "Non supported os"
#endif
}  // namespace

class MmapWriter {
 public:
  MmapWriter(){};
  ~MmapWriter() {
    if (IsOpen()) {
      Close();
    }
  };
  bool IsOpen();
  bool Open(std::string filename, size_t size, int64_t offset);
  bool Close();
  bool CheckCapacity(size_t size);
  bool PrepareWrite(size_t size);
  void Seek(size_t n);
  std::string GetErrorMessage();
  uint8_t* Cursor();
  // private:
  void AddErrorMessage(std::string message);
  void AddErrorMessageWithErrono(std::string message, int errno_value);

  size_t extend_size_       = 4096 * 4 * 2;
  bool verbose_             = false;
  std::string filename_     = "";
  bool is_open_             = false;
  int fd_                   = 0;
  uint8_t* head_            = nullptr;
  size_t aligned_file_size_ = 0;
  size_t map_size_          = 0;
  size_t file_offset_       = 0;
  size_t local_offset_      = 0;
  // NOTE: cursor_ = head_ + local_offset_
  uint8_t* cursor_           = nullptr;
  std::string error_message_ = "";

  // Max OS X(page size is 16384B(16KB))
  const size_t PAGE_SIZE = getpagesize();
  const size_t PAGE_MASK = (PAGE_SIZE - 1);

  size_t PAGE_ALIGEND(size_t x) { return (((x) + PAGE_MASK) & ~PAGE_MASK); }
  size_t PAGE_ALIGEND_ROUND_DOWN(size_t x) { return ((x) & ~PAGE_MASK); }
};

class Logger {
 public:
  explicit Logger(int64_t offset);
  ~Logger();
  void Initialize(int64_t offset);
  void Enter(void* func_address, void* call_site);
  void Exit(void* func_address, void* call_site);
  void Finalize();

  static const int64_t TRUNCATE = 0;
  static const int64_t LAST     = -1;

 private:
  void InternalProcessEnter();
  void InternalProcessExit();
  void InternalProcess(uintptr_t event);
  void ExternalProcessEnter(const std::string& text);
  void ExternalProcessExit(const std::string& text);
  void ExternalProcess(uintptr_t event, const std::string& text);

  MmapWriter mw_;
};

bool MmapWriter::IsOpen() { return is_open_; }

// offset == 0: truncate file
// offset  < 0: seek to last offset, extend size
bool MmapWriter::Open(std::string filename, size_t size, int64_t offset = 0) {
  if (IsOpen()) {
    AddErrorMessage("Open(): already open map");
    return false;
  }
  filename_     = filename;
  int open_flag = O_CREAT | O_RDWR;
  if (offset == 0) {
    open_flag |= O_TRUNC;
  }
  fd_ = open(filename.c_str(), open_flag, 0666);
  if (fd_ < 0) {
    AddErrorMessageWithErrono("Open(): open():", errno);
    return false;
  }

  if (offset < 0) {
    size_t file_size = 0;
    struct stat stbuf;
    if (fstat(fd_, &stbuf) != 0) {
      AddErrorMessageWithErrono("Open(): fstat():", errno);
      return false;
    }
    file_size = stbuf.st_size;
    size += file_size;
    offset = file_size;
  }

  aligned_file_size_ = PAGE_ALIGEND(size);
  map_size_          = PAGE_ALIGEND(aligned_file_size_ - offset);

  if (ftruncate(fd_, aligned_file_size_) != 0) {
    AddErrorMessageWithErrono("Open(): ftruncate():", errno);
    return false;
  }

  size_t aligned_offset = PAGE_ALIGEND_ROUND_DOWN(offset);

  if (verbose_) {
    printf("[Open]\n");
    printf("offset:%lld\n", offset);
    printf("aligned_offset:%zu\n", aligned_offset);
    printf("aligned_file_size_:%zu\n", aligned_file_size_);
    printf("map_size_:%zu\n", map_size_);
  }
  head_ = reinterpret_cast<uint8_t*>(
      mmap(nullptr, map_size_, PROT_WRITE, MAP_SHARED, fd_, aligned_offset));

  if (head_ == MAP_FAILED) {
    AddErrorMessageWithErrono("Open(): ftruncate():", errno);
    return false;
  }
  file_offset_  = offset;
  local_offset_ = offset % PAGE_SIZE;
  cursor_       = reinterpret_cast<uint8_t*>(head_) + local_offset_;
  is_open_      = true;
  if (verbose_) {
    printf("cursor_:%p\n", cursor_);
    printf("file_offset_:%zu\n", file_offset_);
    printf("local_offset_:%zu\n", local_offset_);
  }
  return true;
}
bool MmapWriter::Close() {
  if (!IsOpen()) {
    AddErrorMessage("Close(): no opening map:");
    return false;
  }
  if (verbose_) {
    printf("[Close]\n");
    printf("file_offset_:%zu\n", file_offset_);
    printf("local_offset_:%zu\n", local_offset_);
  }

  if (msync(head_, local_offset_, MS_SYNC) != 0) {
    AddErrorMessageWithErrono("Close(): msync():", errno);
    return false;
  }
  if (munmap(head_, map_size_) != 0) {
    AddErrorMessageWithErrono("Close(): munmap():", errno);
    return false;
  }
  if (ftruncate(fd_, file_offset_) != 0) {
    AddErrorMessageWithErrono("Close(): ftruncate():", errno);
    return false;
  }

  if (close(fd_) != 0) {
    AddErrorMessageWithErrono("Close(): close():", errno);
    return false;
  }
  is_open_ = false;
  return true;
}

bool MmapWriter::CheckCapacity(size_t size) {
  if (local_offset_ + size <= map_size_) {
    return true;
  }
  return false;
}

bool MmapWriter::PrepareWrite(size_t size) {
  if (CheckCapacity(size)) {
    return true;
  }
  if (!Close()) {
    AddErrorMessage("PrepareWrite():");
    return false;
  }
  size_t extend_size = extend_size_;
  if (extend_size < size) {
    extend_size = PAGE_ALIGEND(size);
  }
  size_t new_file_size = aligned_file_size_ + extend_size;
  size_t new_offset =
      PAGE_ALIGEND_ROUND_DOWN(file_offset_) + (local_offset_ % PAGE_SIZE);
  if (!Open(filename_, new_file_size, new_offset)) {
    AddErrorMessage("PrepareWrite():");
    return false;
  }
  return true;
}

void MmapWriter::Seek(size_t n) {
  file_offset_ += n;
  local_offset_ += n;
  cursor_ += n;
}

std::string MmapWriter::GetErrorMessage() {
  std::string tmp = error_message_;
  error_message_.clear();
  return tmp;
}
uint8_t* MmapWriter::Cursor() { return cursor_; }

void MmapWriter::AddErrorMessage(std::string message) {
  error_message_ = message + error_message_;
}
void MmapWriter::AddErrorMessageWithErrono(std::string message,
                                           int errno_value) {
  error_message_ =
      message + std::string(std::strerror(errno)) + ":" + error_message_;
}

// for MmapWriter check
// int main(const int argc, const char* argv[]) {
// bool ret = false;
// MmapWriter mw;
// if (!ret) {
// std::cerr << mw.GetErrorMessage() << std::endl;
// return 1;
// }
// int n = 100;
// for (int i = 0; i < 1000; i++) {
// ret = mw.PrepareWrite(n);
// if (!ret) {
// std::cerr << mw.GetErrorMessage() << std::endl;
// break;
// }
// memset(mw.cursor_, i % 256, n);
// mw.Seek(n);
// }
// ret = mw.Close();
// if (!ret) {
// std::cerr << mw.GetErrorMessage() << std::endl;
// return 1;
// }
// return 0;
// }

namespace {
thread_local pid_t tid = gettid();
// destructors which are called after this logger destructor cannot access this logger variable
thread_local Logger logger(Logger::TRUNCATE);
// I don't know why Apple M1 don't call logger destructor of main thread
#ifdef __APPLE__
struct ForceLoggerDestructor {
  ~ForceLoggerDestructor() {
    if (tls_init_trigger != 0) {
      logger.Finalize();
      tls_init_trigger = 0;
    }
  }
} force_logger_destructor;
#endif
}  // namespace

Logger::Logger(int64_t offset) { Initialize(offset); }
Logger::~Logger() {
  if (tls_init_trigger != 0) {
    Finalize();

    // below value is used to know loggre lifetime
    // the reason why use int is that basic type has no destructor
    tls_init_trigger = 0;
  }
};

void Logger::Initialize(int64_t offset) {
  std::string filename = std::string("iftracer.out.") + std::to_string(tid);
  bool ret             = mw_.Open(filename, 4096 * 4 * 2, offset);
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }
}
void Logger::Finalize() {
  bool ret = mw_.Close();
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }
}

namespace {
constexpr int64_t pointer_size        = sizeof(uintptr_t) * 8;
constexpr uintptr_t flag_mask         = (0x1UL << (pointer_size - 2)) - 1;
constexpr uintptr_t enter_flag        = 0x0UL << (pointer_size - 2);
constexpr uintptr_t exit_flag         = 0x1UL << (pointer_size - 2);
constexpr uintptr_t internal_use_flag = 0x2UL << (pointer_size - 2);
constexpr uintptr_t external_use_flag = 0x3UL << (pointer_size - 2);
uintptr_t set_flag_to_address(uintptr_t address, uintptr_t flag) {
  return (address & flag_mask) | flag;
}
uint64_t get_current_micro_timestamp() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
constexpr uintptr_t internal_process_enter = 0x0UL << (pointer_size - 3);
constexpr uintptr_t internal_process_exit  = 0x1UL << (pointer_size - 3);

constexpr uintptr_t external_process_enter = 0x0UL << (pointer_size - 3);
constexpr uintptr_t external_process_exit  = 0x1UL << (pointer_size - 3);
}  // namespace

void Logger::InternalProcessEnter() { InternalProcess(internal_process_enter); }
void Logger::InternalProcessExit() { InternalProcess(internal_process_exit); }

void Logger::InternalProcess(uintptr_t event) {
#ifdef IFTRACE_TEXT_FORMAT
#else
  // TODO: add cpu clock pattern
  uint64_t micro_since_epoch = get_current_micro_timestamp();
  int max_n                  = 256;
  if (!mw_.CheckCapacity(max_n) && !mw_.PrepareWrite(max_n)) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }

  *reinterpret_cast<uint64_t*>(mw_.Cursor()) = micro_since_epoch;
  mw_.Seek(sizeof(uint64_t));
  uintptr_t masked_func_address = set_flag_to_address(
      reinterpret_cast<uintptr_t>(event), internal_use_flag);
  *reinterpret_cast<uintptr_t*>(mw_.Cursor()) = masked_func_address;
  mw_.Seek(sizeof(uintptr_t));
#endif
}

void Logger::ExternalProcessEnter(const std::string& text) {
  ExternalProcess(external_process_enter, text);
}
void Logger::ExternalProcessExit(const std::string& text) {
  ExternalProcess(external_process_exit, text);
}

void Logger::ExternalProcess(uintptr_t event, const std::string& text) {
#ifdef IFTRACE_TEXT_FORMAT
#else
  // TODO: add cpu clock pattern
  uint64_t micro_since_epoch = get_current_micro_timestamp();
  constexpr int align_buffer = 8;
  int max_n = sizeof(uint64_t) + sizeof(uintptr_t) + text.size() + align_buffer;
  if (!mw_.CheckCapacity(max_n) && !mw_.PrepareWrite(max_n)) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }

  *reinterpret_cast<uint64_t*>(mw_.Cursor()) = micro_since_epoch;
  mw_.Seek(sizeof(uint64_t));

  size_t text_size           = text.size();
  uintptr_t masked_text_size = set_flag_to_address(
      reinterpret_cast<uintptr_t>(text_size), external_use_flag);
  *reinterpret_cast<uintptr_t*>(mw_.Cursor()) = masked_text_size;
  mw_.Seek(sizeof(uintptr_t));

  text.copy(reinterpret_cast<char*>(mw_.Cursor()), text_size);
  size_t aligned_text_size = (((text_size) + (8 - 1)) & ~(8 - 1));
  mw_.Seek(aligned_text_size);
#endif
}

void Logger::Enter(void* func_address, void* call_site) {
  // printf("[%d][%"PRIu64"][trace func][enter]:%p call %p\n", tid, micro_since_epoch, call_site, func_address);
  int max_n = 256;
  if (!mw_.CheckCapacity(max_n)) {
    InternalProcessEnter();
    if (!mw_.PrepareWrite(max_n)) {
      std::cerr << mw_.GetErrorMessage() << std::endl;
      InternalProcessExit();
      return;
    }
    InternalProcessExit();
  }

  // TODO: add cpu clock pattern
  uint64_t micro_since_epoch = get_current_micro_timestamp();
  // arm thumb mode use LSB
  // Odd addresses for Thumb mode, and even addresses for ARM mode.
  void* normalized_func_address =
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(func_address) & (~1));
#ifdef IFTRACE_TEXT_FORMAT
  // TODO: add binary write pattern
  int n = snprintf(reinterpret_cast<char*>(mw_.Cursor()), max_n,
                   "%d %" PRIu64 " enter %p %p\n", tid, micro_since_epoch,
                   call_site, normalized_func_address);
  mw_.Seek(n);
#else
  *reinterpret_cast<uint64_t*>(mw_.Cursor()) = micro_since_epoch;
  mw_.Seek(sizeof(uint64_t));
  uintptr_t masked_func_address = set_flag_to_address(
      reinterpret_cast<uintptr_t>(normalized_func_address), enter_flag);
  *reinterpret_cast<uintptr_t*>(mw_.Cursor()) = masked_func_address;
  mw_.Seek(sizeof(uintptr_t));
#endif
}

void Logger::Exit(void* func_address, void* call_site) {
  // printf("[%d][%"PRIu64"][trace func][exit]:%p call %p\n", tid, micro_since_epoch, call_site, func_address);
  int max_n = 256;
  if (!mw_.CheckCapacity(max_n)) {
    InternalProcessEnter();
    if (!mw_.PrepareWrite(max_n)) {
      std::cerr << mw_.GetErrorMessage() << std::endl;
      InternalProcessExit();
      return;
    }
    InternalProcessExit();
  }

  // TODO: add cpu clock pattern
  uint64_t micro_since_epoch = get_current_micro_timestamp();
  void* normalized_func_address =
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(func_address) & (~1));
#ifdef IFTRACE_TEXT_FORMAT
  // TODO: add binary write pattern
  int n = snprintf(reinterpret_cast<char*>(mw_.Cursor()), max_n,
                   "%d %" PRIu64 " exit %p %p\n", tid, micro_since_epoch,
                   call_site, normalized_func_address);
  mw_.Seek(n);
#else
  *reinterpret_cast<uint64_t*>(mw_.Cursor()) = micro_since_epoch;
  mw_.Seek(sizeof(uint64_t));
  uintptr_t masked_func_address = set_flag_to_address(
      reinterpret_cast<uintptr_t>(normalized_func_address), exit_flag);
  *reinterpret_cast<uintptr_t*>(mw_.Cursor()) = masked_func_address;
  mw_.Seek(sizeof(uintptr_t));
#endif
}

// const char* __attribute__((no_instrument_function)) addr2name(void* address) {
// Dl_info dli;
// if (dladdr(address, &dli) != 0) {
// return dli.dli_sname;
// }
// return nullptr;
// }
void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void* func_address, void* call_site) {
  if (tls_init_trigger != 0) {
    logger.Enter(func_address, call_site);
  } else {
    // after thread_local logger destructor called
    Logger logger(Logger::LAST);
    logger.Enter(func_address, call_site);
    // printf("[call after tracer destructor][enter][%d]: %p calls %p\n", tid, call_site, func_address);
  }
  // const char* func_name = addr2name(func_address);
  // if (func_name) {
  // printf("[trace func][enter]:%s\n", func_name);
  // }
}

void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void* func_address, void* call_site) {
  if (tls_init_trigger != 0) {
    logger.Exit(func_address, call_site);
  } else {
    // after thread_local logger destructor called
    Logger logger(Logger::LAST);
    logger.Exit(func_address, call_site);
    // printf("[call after tracer destructor][ exit][%d]: %p calls %p\n", tid, call_site, func_address);
  }
  // const char* func_name = addr2name(func_address);
  // if (func_name) {
  // printf("[trace func][exit ]:%s\n", func_name);
  // }
}
