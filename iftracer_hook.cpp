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

#include <inttypes.h>
#include <sys/syscall.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

#include "mmap_writer.hpp"

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

uint64_t get_current_micro_timestamp() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

namespace {
// NOTE: Max OS X(page size is 16384B(16KB))
size_t get_init_buffer_size() {
  static size_t init_buffer_size = 0;
  if (init_buffer_size != 0) {
    return init_buffer_size;
  }

  char* env;
  env = getenv("IFTRACER_INIT_BUFFER");
  if (env != nullptr) {
    init_buffer_size = 4096 * std::stoi(std::string(env));
  }
  if (init_buffer_size < 4096 * 4 * 1024) {
    init_buffer_size = 4096 * 4 * 1024;
  }
  return init_buffer_size;
}
size_t get_extend_buffer_size() {
  static size_t extend_buffer_size = 0;
  if (extend_buffer_size != 0) {
    return extend_buffer_size;
  }

  char* env;
  env = getenv("IFTRACER_EXTEND_BUFFER");
  if (env != nullptr) {
    extend_buffer_size = 4096 * std::stoi(std::string(env));
  }
  if (extend_buffer_size < 4096 * 4 * 2) {
    extend_buffer_size = 4096 * 4 * 2;
  }
  return extend_buffer_size;
}
size_t get_flush_buffer_size() {
  static size_t flush_buffer_size = 0;
  if (flush_buffer_size != 0) {
    return flush_buffer_size;
  }

  char* env;
  env = getenv("IFTRACER_FLUSH_BUFFER");
  if (env != nullptr) {
    flush_buffer_size = 4096 * std::stoi(std::string(env));
  }
  if (flush_buffer_size < 4096 * 4 * 16) {
    flush_buffer_size = 4096 * 4 * 16;
  }
  return flush_buffer_size;
}
std::string get_output_directory() {
  static std::string output_directory = "";
  if (!output_directory.empty()) {
    return output_directory;
  }

  char* env;
  env = getenv("IFTRACER_OUTPUT_DIRECTORY");
  if (env != nullptr) {
    output_directory = std::string(env);
  } else {
    output_directory = "./";
  }
  return output_directory;
}
std::string get_output_file_prefix() {
  static std::string output_file_prefix = "";
  if (!output_file_prefix.empty()) {
    return output_file_prefix;
  }

  char* env;
  env = getenv("IFTRACER_OUTPUT_FILE_PREFIX");
  if (env != nullptr) {
    output_file_prefix = std::string(env);
  } else {
    output_file_prefix = "iftracer.out.";
  }
  return output_file_prefix;
}
}  // namespace

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

  void ExternalProcessEnter(const std::string& text);
  void ExternalProcessExit(const std::string& text);
  void ExternalProcess(uintptr_t event, const std::string& text);

 private:
  void InternalProcessEnter();
  void InternalProcessExit();
  void InternalProcess(uintptr_t event);

  MmapWriter mw_;
  size_t flush_buffer_size_;
};

namespace {
thread_local pid_t tid              = gettid();
thread_local uint64_t pre_timestamp = get_current_micro_timestamp();

int32_t get_current_micro_timestamp_diff() {
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  constexpr uint64_t timestamp_diff_offset = 1;
  uint64_t timestamp_diff = timestamp - pre_timestamp + timestamp_diff_offset;
  pre_timestamp           = timestamp;
  return static_cast<int32_t>(timestamp_diff);
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
}
void Logger::Finalize() {
  bool ret = mw_.Close();
  if (!ret) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
  }
}

namespace {
constexpr int64_t pointer_size = sizeof(uintptr_t) * 8;
constexpr uintptr_t flag_mask  = (0x1UL << (pointer_size - 2)) - 1;
constexpr uintptr_t enter_flag = 0x0UL << (pointer_size - 2);
// constexpr uintptr_t exit_flag         = 0x1UL << (pointer_size - 2);
constexpr uintptr_t internal_use_flag = 0x2UL << (pointer_size - 2);
constexpr uintptr_t external_use_flag = 0x3UL << (pointer_size - 2);
uintptr_t set_flag_to_address(uintptr_t address, uintptr_t flag) {
  return (address & flag_mask) | flag;
}

constexpr uintptr_t internal_process_enter_exit_mask = 0x1UL
                                                       << (pointer_size - 3);
constexpr uintptr_t internal_process_enter = 0x0UL << (pointer_size - 3);
constexpr uintptr_t internal_process_exit  = 0x1UL << (pointer_size - 3);

constexpr uintptr_t external_process_enter_exit_mask = 0x1UL
                                                       << (pointer_size - 3);
constexpr uintptr_t external_process_enter = 0x0UL << (pointer_size - 3);
constexpr uintptr_t external_process_exit  = 0x1UL << (pointer_size - 3);
}  // namespace

namespace iftracer {
void ExternalProcessEnter(const std::string& text);
void ExternalProcessExit(const std::string& text);

void ExternalProcessEnter(const std::string& text) {
  logger.ExternalProcessEnter(text);
}
void ExternalProcessExit(const std::string& text) {
  logger.ExternalProcessExit(text);
}
}  // namespace iftracer

void Logger::InternalProcessEnter() { InternalProcess(internal_process_enter); }
void Logger::InternalProcessExit() { InternalProcess(internal_process_exit); }

void Logger::InternalProcess(uintptr_t event) {
#ifdef IFTRACE_TEXT_FORMAT
#else
  int32_t micro_duration_diff = get_current_micro_timestamp_diff();
  int max_n                   = 256;
  if (!mw_.CheckCapacity(max_n) && !mw_.PrepareWrite(max_n)) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }

  if ((event & internal_process_enter_exit_mask) == internal_process_enter) {
    *reinterpret_cast<int32_t*>(mw_.Cursor()) = micro_duration_diff;
    mw_.Seek(sizeof(int32_t));
    uintptr_t masked_func_address = set_flag_to_address(
        reinterpret_cast<uintptr_t>(event), internal_use_flag);
    *reinterpret_cast<uintptr_t*>(mw_.Cursor()) = masked_func_address;
    mw_.Seek(sizeof(uintptr_t));
  } else if ((event & internal_process_enter_exit_mask) ==
             internal_process_exit) {
    *reinterpret_cast<int32_t*>(mw_.Cursor()) = -micro_duration_diff;
    mw_.Seek(sizeof(int32_t));
  } else {
    fprintf(stderr, "invalid internal event flag %" PRIxPTR "\n", event);
    abort();
  }
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
  int32_t micro_duration_diff = get_current_micro_timestamp_diff();
  constexpr int align_buffer  = 8;
  int max_n = sizeof(int32_t) + sizeof(uintptr_t) + text.size() + align_buffer;
  if (!mw_.CheckCapacity(max_n) && !mw_.PrepareWrite(max_n)) {
    std::cerr << mw_.GetErrorMessage() << std::endl;
    return;
  }

  if ((event & external_process_enter_exit_mask) == external_process_enter) {
    *reinterpret_cast<int32_t*>(mw_.Cursor()) = micro_duration_diff;
    mw_.Seek(sizeof(int32_t));

    size_t text_size           = text.size();
    uintptr_t masked_text_size = set_flag_to_address(
        reinterpret_cast<uintptr_t>(event | text_size), external_use_flag);
    *reinterpret_cast<uintptr_t*>(mw_.Cursor()) = masked_text_size;
    mw_.Seek(sizeof(uintptr_t));

    text.copy(reinterpret_cast<char*>(mw_.Cursor()), text_size);
    size_t aligned_text_size = (((text_size) + (8 - 1)) & ~(8 - 1));
    mw_.Seek(aligned_text_size);
  } else if ((event & external_process_enter_exit_mask) ==
             external_process_exit) {
    *reinterpret_cast<int32_t*>(mw_.Cursor()) = -micro_duration_diff;
    mw_.Seek(sizeof(int32_t));
  } else {
    fprintf(stderr, "invalid external event flag %" PRIxPTR "\n", event);
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
  int32_t micro_duration_diff = get_current_micro_timestamp_diff();
  *reinterpret_cast<int32_t*>(mw_.Cursor()) = micro_duration_diff;
  mw_.Seek(sizeof(int32_t));
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
  int32_t micro_duration_diff = get_current_micro_timestamp_diff();
  *reinterpret_cast<int32_t*>(mw_.Cursor()) = -micro_duration_diff;
  mw_.Seek(sizeof(int32_t));
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
