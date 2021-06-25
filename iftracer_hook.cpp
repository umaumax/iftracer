thread_local int tls_init_trigger = 1;
class FastLoggerCaller {
 public:
  FastLoggerCaller(int dummy) {}
  // use blank destructor for supressing fast_logger_caller variable by -O3 optimization
  ~FastLoggerCaller() {}
};
namespace {
FastLoggerCaller fast_logger_caller(tls_init_trigger);
}

#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#define FILE_SIZE 4096 * 1024

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
  bool Open(std::string filename, size_t size, size_t offset);
  bool Close();
  bool PrepareWrite(size_t size);
  void Seek(size_t n);
  std::string GetErrorMessage();
  uint8_t* Cursor();
  // private:
  void AddErrorMessage(std::string message);
  void AddErrorMessageWithErrono(std::string message, int errno_value);

  size_t extend_size_   = 4096 * 10;
  size_t page_size_     = getpagesize();
  bool verbose_         = false;
  std::string filename_ = "";
  bool is_open_         = false;
  int fd_               = 0;
  uint8_t* head_        = nullptr;
  size_t file_size_     = 0;
  size_t map_size_      = 0;
  size_t file_offset_   = 0;
  size_t local_offset_  = 0;
  // NOTE: cursor_ = head_ + local_offset_
  uint8_t* cursor_           = nullptr;
  std::string error_message_ = "";
};

class Logger {
 public:
  Logger();
  ~Logger();
  void Enter(void* func_address, void* call_site);
  void Exit(void* func_address, void* call_site);

 private:
  MmapWriter mw_;
};

bool MmapWriter::IsOpen() { return is_open_; }

bool MmapWriter::Open(std::string filename, size_t size, size_t offset = 0) {
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

  file_size_ = ((size + page_size_ - 1) / page_size_) * page_size_;
  map_size_ =
      ((file_size_ - offset + page_size_ - 1) / page_size_) * page_size_;

  if (ftruncate(fd_, file_size_) != 0) {
    AddErrorMessageWithErrono("Open(): ftruncate():", errno);
    return false;
  }

  size_t aligned_offset = (offset / page_size_) * page_size_;

  if (verbose_) {
    printf("[Open]\n");
    printf("offset:%zu\n", offset);
    printf("aligned_offset:%zu\n", aligned_offset);
    printf("file_size_:%zu\n", file_size_);
    printf("map_size_:%zu\n", map_size_);
  }
  head_ = reinterpret_cast<uint8_t*>(
      mmap(nullptr, map_size_, PROT_WRITE, MAP_SHARED, fd_, aligned_offset));

  if (head_ == MAP_FAILED) {
    AddErrorMessageWithErrono("Open(): ftruncate():", errno);
    return false;
  }
  file_offset_  = offset;
  local_offset_ = offset % 4096;
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

bool MmapWriter::PrepareWrite(size_t size) {
  if (local_offset_ + size <= map_size_) {
    return true;
  }
  if (!Close()) {
    AddErrorMessage("PrepareWrite():");
    return false;
  }
  size_t extend_size = extend_size_;
  if (extend_size < size) {
    extend_size = ((size + 4095) / 4096) * 4096;
  }
  size_t new_file_size = file_size_ + extend_size;
  size_t new_offset    = (file_offset_ / 4096) * 4096 + (local_offset_ % 4096);
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
thread_local Logger logger;
}  // namespace

Logger::Logger() {
  std::string filename = std::string("iftracer.out.") + std::to_string(tid);
  bool ret             = mw_.Open(filename, FILE_SIZE);
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }
}
Logger::~Logger() {
  bool ret = mw_.Close();
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }

  // below value is used to know loggre lifetime
  // the reason why use int is that basic type has no destructor
  tls_init_trigger = 0;
};

void Logger::Enter(void* func_address, void* call_site) {
  // TODO: add cpu clock pattern
  uint64_t micro_since_epoch =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  // printf("[%d][%"PRIu64"][trace func][enter]:%p call %p\n", tid, micro_since_epoch, call_site, func_address);
  int max_n = 256;
  bool ret  = mw_.PrepareWrite(max_n);
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }
  // TODO: add binary write pattern
  int n = snprintf(reinterpret_cast<char*>(mw_.Cursor()), max_n,
                   "%d %" PRIu64 " enter %p %p\n", tid, micro_since_epoch,
                   call_site, func_address);
  mw_.Seek(n);
}

void Logger::Exit(void* func_address, void* call_site) {
  // TODO: add cpu clock pattern
  uint64_t micro_since_epoch =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  // printf("[%d][%"PRIu64"][trace func][exit]:%p call %p\n", tid, micro_since_epoch, call_site, func_address);
  int max_n = 256;
  bool ret  = mw_.PrepareWrite(max_n);
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }
  // TODO: add binary write pattern
  int n = snprintf(reinterpret_cast<char*>(mw_.Cursor()), max_n,
                   "%d %" PRIu64 " exit %p %p\n", tid, micro_since_epoch,
                   call_site, func_address);
  mw_.Seek(n);
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
    Logger logger;
    logger.Enter(func_address, call_site);
    // printf("[call after tracer destructor][enter]: %p calls %p\n", call_site,
    // func_address);
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
    Logger logger;
    logger.Exit(func_address, call_site);
    // printf("[call after tracer destructor][ exit]: %p calls %p\n", call_site,
    // func_address);
  }
  // const char* func_name = addr2name(func_address);
  // if (func_name) {
  // printf("[trace func][exit ]:%s\n", func_name);
  // }
}
