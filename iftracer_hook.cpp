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

extern "C" {
void __cyg_profile_func_enter(void* func_address, void* call_site);
void __cyg_profile_func_exit(void* func_address, void* call_site);
}

namespace {
pid_t __attribute__((no_instrument_function)) gettid() {
  return syscall(SYS_gettid);
}
}  // namespace

class MmapWriter {
 public:
  MmapWriter(){};
  ~MmapWriter() {
    if (IsOpen()) {
      Close();
    }
  };

  bool IsOpen() { return is_open_; }

  bool Open(std::string filename, size_t size, size_t offset = 0) {
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
      std::cout << "[Open]" << std::endl;
      std::cout << "offset:" << offset << std::endl;
      std::cout << "aligned_offset:" << aligned_offset << std::endl;
      std::cout << "file_size_:" << file_size_ << std::endl;
      std::cout << "map_size_:" << map_size_ << std::endl;
    }
    head_ = reinterpret_cast<uint8_t*>(
        mmap(nullptr, map_size_, PROT_WRITE, MAP_SHARED, fd_, aligned_offset));

    if (head_ == MAP_FAILED) {
      AddErrorMessageWithErrono("Open(): ftruncate():", errno);
      return false;
    }
    cursor_       = reinterpret_cast<uint8_t*>(head_);
    file_offset_  = offset;
    local_offset_ = offset % 4096;
    is_open_      = true;
    return true;
  }
  bool Close() {
    if (!IsOpen()) {
      AddErrorMessage("Close(): no opening map:");
      return false;
    }
    if (verbose_) {
      std::cout << "[Close]" << std::endl;
      std::cout << "file_offset_:" << file_offset_ << std::endl;
      std::cout << "local_offset_:" << local_offset_ << std::endl;
    }
    msync(head_, local_offset_, MS_SYNC);
    if (ftruncate(fd_, file_offset_) != 0) {
      AddErrorMessageWithErrono("Close(): ftruncate():", errno);
      return false;
    }

    close(fd_);
    munmap(head_, map_size_);
    is_open_ = false;
    return true;
  }

  bool PrepareWrite(size_t size) {
    if (local_offset_ + size <= map_size_) {
      return true;
    }
    if (!Close()) {
      AddErrorMessage("PrepareWrite():");
      return false;
    }
    size_t extend_size = 4096 * 10;
    if (extend_size < size) {
      extend_size = ((size + 4095) / 4096) * 4096;
    }
    size_t new_file_size = file_size_ + extend_size;
    size_t new_offset = (file_offset_ / 4096) * 4096 + (local_offset_ % 4096);
    if (!Open(filename_, new_file_size, new_offset)) {
      AddErrorMessage("PrepareWrite():");
      return false;
    }
    return true;
  }

  void Seek(size_t n) {
    file_offset_ += n;
    local_offset_ += n;
    cursor_ += n;
  }

  std::string GetErrorMessage() {
    std::string tmp = error_message_;
    error_message_.clear();
    return tmp;
  }
  uint8_t* Cursor() { return cursor_; }

  // private:
  void AddErrorMessage(std::string message) {
    error_message_ = message + error_message_;
  }
  void AddErrorMessageWithErrono(std::string message, int errno_value) {
    error_message_ =
        message + std::string(std::strerror(errno)) + ":" + error_message_;
  }

  bool verbose_ = false;
  std::string filename_;
  bool is_open_              = false;
  int fd_                    = 0;
  uint8_t* head_             = nullptr;
  size_t file_size_          = 0;
  size_t page_size_          = getpagesize();
  size_t map_size_           = 0;
  size_t file_offset_        = 0;
  size_t local_offset_       = 0;
  uint8_t* cursor_           = nullptr;
  std::string error_message_ = "";
};

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

#define FILE_SIZE 1024 * 1024
class Logger {
 public:
  Logger();
  ~Logger();
  void Enter(void* func_address, void* call_site);
  void Exit(void* func_address, void* call_site);

 private:
  MmapWriter mw_;
};
namespace {
thread_local pid_t tid = gettid();
// __attribute__((init_priority(101)))
// を追加してもSEGVするのは、thread_localであるため(シングルスレッドアプリケーションに対して、thread_local無し版で検証済み)
// thread_local __attribute__((init_priority(101))) Logger logger;
// 理想的には、このLoggerが一番初めに生成されて、一番最後に破棄されることであるが、
// LD_PRELOAD利用時などを含めてこの保証は実現不可能
// このLoggerよりも後に廃棄されるデストラクタはtracerしてもログに出力できない状態
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
  // ライフタイムをこの値を利用して管理する(デストラクタが呼び出されない、基本型を利用する);
  tid = 0;
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
  if (tid != 0) {
    logger.Enter(func_address, call_site);
  } else {
    printf("[call after tracer destructor][enter]: %p calls %p\n", call_site,
           func_address);
  }
  // const char* func_name = addr2name(func_address);
  // if (func_name) {
  // printf("[trace func][enter]:%s\n", func_name);
  // }
}

void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void* func_address, void* call_site) {
  if (tid != 0) {
    logger.Exit(func_address, call_site);
  } else {
    printf("[call after tracer destructor][ exit]: %p calls %p\n", call_site,
           func_address);
  }
  // const char* func_name = addr2name(func_address);
  // if (func_name) {
  // printf("[trace func][exit ]:%s\n", func_name);
  // }
}
