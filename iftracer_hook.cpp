thread_local int tls_init_trigger = 1;
class FastLoggerCaller {
 public:
  FastLoggerCaller(int dummy) {}
  // use blank destructor for supressing fast_logger_caller variable
  // by -O3 optimization
  ~FastLoggerCaller() {}
};
namespace {
#if __linux__
__attribute__((init_priority(101)))
#endif
FastLoggerCaller fast_logger_caller(tls_init_trigger);
}  // namespace

#include <inttypes.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>

#include "mmap_writer.hpp"
#include "queue_worker.hpp"

extern "C" {
void __cyg_profile_func_enter(void* func_address, void* call_site);
void __cyg_profile_func_exit(void* func_address, void* call_site);
}

namespace {
void exit_handler(int sig) { std::exit(128 + sig); }
struct ExitBySignal {
  ExitBySignal() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = exit_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
  }
} exit_by_signal;

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

uint64_t get_current_micro_timestamp() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

namespace {
// NOTE: Max OS X(page size is 16384B(16KB))
size_t get_init_buffer_size() {
  static size_t init_buffer_size = []() {
    size_t init_buffer_size = 0;
    char* env               = getenv("IFTRACER_INIT_BUFFER");
    if (env != nullptr) {
      init_buffer_size = 4096 * std::stoi(std::string(env));
    }
    if (init_buffer_size < 4096 * 4 * 1024) {
      init_buffer_size = 4096 * 4 * 1024;
    }
    return init_buffer_size;
  }();
  return init_buffer_size;
}
size_t get_extend_buffer_size() {
  static size_t extend_buffer_size = []() {
    size_t extend_buffer_size = 0;
    char* env                 = getenv("IFTRACER_EXTEND_BUFFER");
    if (env != nullptr) {
      extend_buffer_size = 4096 * std::stoi(std::string(env));
    }
    if (extend_buffer_size < 4096 * 4 * 2) {
      extend_buffer_size = 4096 * 4 * 2;
    }
    return extend_buffer_size;
  }();
  return extend_buffer_size;
}
size_t get_flush_buffer_size() {
  static size_t flush_buffer_size = []() {
    size_t flush_buffer_size = 0;
    char* env                = getenv("IFTRACER_FLUSH_BUFFER");
    if (env != nullptr) {
      flush_buffer_size = 4096 * std::stoi(std::string(env));
    }
    if (flush_buffer_size < 4096 * 4 * 16) {
      flush_buffer_size = 4096 * 4 * 16;
    }
    return flush_buffer_size;
  }();
  return flush_buffer_size;
}
std::string get_output_directory() {
  static std::string output_directory = []() {
    char* env = getenv("IFTRACER_OUTPUT_DIRECTORY");
    if (env != nullptr) {
      return std::string(env);
    }
    return std::string("./");
  }();
  return output_directory;
}
std::string get_output_file_prefix() {
  static std::string output_file_prefix = []() {
    char* env = getenv("IFTRACER_OUTPUT_FILE_PREFIX");
    if (env != nullptr) {
      return std::string(env);
    }
    return std::string("iftracer.out.");
  }();
  return output_file_prefix;
}

bool get_async_munmap_flag() {
  static bool async_munmap_flag = []() {
    char* env = getenv("IFTRACER_ASYNC_MUNMAP");
    if (env != nullptr) {
      return std::stoi(env) != 0;
    }
    return false;
  }();
  return async_munmap_flag;
}
}  // namespace

namespace {
using ExtraInfo = uint32_t;
}

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

  void ExternalProcessEnter();
  void ExternalProcessExit(const std::string& text);
  void ExternalProcess(ExtraInfo event, const std::string& text);

 private:
  void InternalProcessEnter();
  void InternalProcessExit();
  void InternalProcess(ExtraInfo event);

  MmapWriter mw_;
  size_t flush_buffer_size_;
};

namespace {
thread_local pid_t tid = gettid();
uint64_t get_base_timestamp() {
  static uint64_t base_timestamp = get_current_micro_timestamp();
  return base_timestamp;
};
thread_local uint64_t pre_timestamp = get_base_timestamp();

uint32_t get_current_micro_timestamp_diff_with_offset() {
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  constexpr uint64_t timestamp_diff_offset = 1;
  uint64_t timestamp_diff = timestamp - pre_timestamp + timestamp_diff_offset;
  pre_timestamp           = timestamp;
  return static_cast<uint32_t>(timestamp_diff);
}

std::function<int(void*, size_t)> get_async_munmap_func() {
  static iftracer::QueueWorker<std::tuple<void*, size_t>> munmaper(
      1, [](std::tuple<void*, size_t> arg) {
        void*& addr    = std::get<0>(arg);
        size_t& length = std::get<1>(arg);
        if (munmap(addr, length) != 0) {
          // no error handling
          return;
        }
      });
  return [](void* addr, size_t length) {
    std::tuple<void*, size_t> arg = std::make_tuple(addr, length);
    munmaper.Post(arg);
    return 0;
  };
}

// WARN: destructors which are called after this logger destructor cannot access this logger variable
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
  std::string filename = get_output_directory() + "/" +
                         get_output_file_prefix() + std::to_string(tid);
  mw_.SetExtendSize(get_extend_buffer_size());
  size_t buffer_size = 4096 * 4;  // used only for last extend
  if (offset >= 0) {
    buffer_size = get_init_buffer_size();
  }
  bool ret = mw_.Open(filename, buffer_size, offset);
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }
  flush_buffer_size_ = get_flush_buffer_size();
  if (get_async_munmap_flag()) {
    printf("hoge\n");
    mw_.SetMunmapHook(get_async_munmap_func());
  }
}
void Logger::Finalize() {
  bool ret = mw_.Close();
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }
}

namespace {
constexpr uint32_t timestamp_size     = sizeof(uint32_t) * 8;
constexpr ExtraInfo unset_flag_mask   = (0x1UL << (timestamp_size - 2)) - 1;
constexpr ExtraInfo flag_mask         = ~unset_flag_mask;
constexpr ExtraInfo normal_enter_flag = 0x0UL << (timestamp_size - 2);
constexpr ExtraInfo internal_or_external_enter_flag = 0x1UL
                                                      << (timestamp_size - 2);
constexpr ExtraInfo internal_or_normal_exit_flag = 0x2UL
                                                   << (timestamp_size - 2);
constexpr ExtraInfo external_exit_flag = 0x3UL << (timestamp_size - 2);
uint32_t set_flag_to_timestamp(uint32_t timestamp, ExtraInfo flag) {
  return (timestamp & unset_flag_mask) | flag;
}

// bool IsNormalEnter(ExtraInfo event) {
// return (event & flag_mask) == normal_enter_flag;
// }
// bool IsNormalExit(ExtraInfo event) {
// return (event & flag_mask) == internal_or_normal_exit_flag;
// }
bool IsInternalEnter(ExtraInfo event) {
  return (event & flag_mask) == internal_or_external_enter_flag;
}
bool IsInternalExit(ExtraInfo event) {
  return (event & flag_mask) == internal_or_normal_exit_flag;
}
bool IsExternalEnter(ExtraInfo event) {
  return (event & flag_mask) == internal_or_external_enter_flag;
}
bool IsExternalExit(ExtraInfo event) {
  return (event & flag_mask) == external_exit_flag;
}
}  // namespace

namespace iftracer {
void ExternalProcessEnter();
void ExternalProcessExit(const std::string& text);

void ExternalProcessEnter() { logger.ExternalProcessEnter(); }
void ExternalProcessExit(const std::string& text) {
  logger.ExternalProcessExit(text);
}
}  // namespace iftracer

void Logger::InternalProcessEnter() {
  InternalProcess(internal_or_external_enter_flag);
}
void Logger::InternalProcessExit() {
  InternalProcess(internal_or_normal_exit_flag);
}

void Logger::InternalProcess(ExtraInfo event) {
#ifdef IFTRACE_TEXT_FORMAT
#else
  uint32_t micro_duration_diff = get_current_micro_timestamp_diff_with_offset();
  int max_n                    = 256;
  if (!mw_.CheckCapacity(max_n) && !mw_.PrepareWrite(max_n)) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }

  *reinterpret_cast<uint32_t*>(mw_.Cursor()) =
      set_flag_to_timestamp(micro_duration_diff, event);
  mw_.Seek(sizeof(uint32_t));
  if (IsInternalEnter(event)) {
    // nothing
  } else if (IsInternalExit(event)) {
    // nothing
  } else {
    fprintf(stderr, "invalid internal event flag %u\n", event);
    abort();
  }
#endif
}

void Logger::ExternalProcessEnter() {
  ExternalProcess(internal_or_external_enter_flag, "");
}
void Logger::ExternalProcessExit(const std::string& text) {
  ExternalProcess(external_exit_flag, text);
}

void Logger::ExternalProcess(ExtraInfo event, const std::string& text) {
#ifdef IFTRACE_TEXT_FORMAT
#else
  uint32_t micro_duration_diff = get_current_micro_timestamp_diff_with_offset();
  constexpr int text_align     = 4;
  int max_n = sizeof(uint32_t) + sizeof(int32_t) + text.size() + text_align;
  if (!mw_.CheckCapacity(max_n) && !mw_.PrepareWrite(max_n)) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }

  *reinterpret_cast<uint32_t*>(mw_.Cursor()) =
      set_flag_to_timestamp(micro_duration_diff, event);
  mw_.Seek(sizeof(uint32_t));
  if (IsExternalEnter(event)) {
  } else if (IsExternalExit(event)) {
    int32_t text_size                         = text.size();
    *reinterpret_cast<int32_t*>(mw_.Cursor()) = text_size;
    mw_.Seek(sizeof(int32_t));

    text.copy(reinterpret_cast<char*>(mw_.Cursor()), text_size);
    size_t aligned_text_size =
        (((text_size) + (text_align - 1)) & ~(text_align - 1));
    mw_.Seek(aligned_text_size);
  } else {
    fprintf(stderr, "invalid external event flag %u\n", event);
    abort();
  }
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

  // arm thumb mode use LSB
  // Odd addresses for Thumb mode, and even addresses for ARM mode.
  void* normalized_func_address =
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(func_address) & (~1));
#ifdef IFTRACE_TEXT_FORMAT
  uint64_t micro_since_epoch = get_current_micro_timestamp();
  int n = snprintf(reinterpret_cast<char*>(mw_.Cursor()), max_n,
                   "%d %" PRIu64 " enter %p %p\n", tid, micro_since_epoch,
                   call_site, normalized_func_address);
  mw_.Seek(n);
#else
  uint32_t micro_duration_diff = get_current_micro_timestamp_diff_with_offset();
  *reinterpret_cast<uint32_t*>(mw_.Cursor()) =
      set_flag_to_timestamp(micro_duration_diff, normal_enter_flag);
  mw_.Seek(sizeof(uint32_t));
  *reinterpret_cast<uintptr_t*>(mw_.Cursor()) =
      reinterpret_cast<uintptr_t>(normalized_func_address);
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
  } else {
    size_t flush_buffer_size = flush_buffer_size_;
    if (mw_.BufferedDataSize() >= flush_buffer_size) {
      InternalProcessEnter();
      mw_.Flush(flush_buffer_size);
      InternalProcessExit();
    }
  }

#ifdef IFTRACE_TEXT_FORMAT
  void* normalized_func_address =
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(func_address) & (~1));
  uint64_t micro_since_epoch = get_current_micro_timestamp();
  int n = snprintf(reinterpret_cast<char*>(mw_.Cursor()), max_n,
                   "%d %" PRIu64 " exit %p %p\n", tid, micro_since_epoch,
                   call_site, normalized_func_address);
  mw_.Seek(n);
#else
  uint32_t micro_duration_diff = get_current_micro_timestamp_diff_with_offset();
  *reinterpret_cast<uint32_t*>(mw_.Cursor()) =
      set_flag_to_timestamp(micro_duration_diff, internal_or_normal_exit_flag);
  mw_.Seek(sizeof(uint32_t));
#endif
}

void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void* func_address, void* call_site) {
  if (tls_init_trigger != 0) {
    logger.Enter(func_address, call_site);
  } else {
    // after thread_local logger destructor called
    Logger logger(Logger::LAST);
    logger.Enter(func_address, call_site);
    logger.Finalize();
    // printf("[call after tracer destructor][enter][%d]: %p calls %p\n", tid, call_site, func_address);
  }
}

void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void* func_address, void* call_site) {
  if (tls_init_trigger != 0) {
    logger.Exit(func_address, call_site);
  } else {
    // after thread_local logger destructor called
    Logger logger(Logger::LAST);
    logger.Exit(func_address, call_site);
    logger.Finalize();
    // printf("[call after tracer destructor][ exit][%d]: %p calls %p\n", tid, call_site, func_address);
  }
}
