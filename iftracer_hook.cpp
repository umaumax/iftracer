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
}  // namespace

namespace {
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
  if (init_buffer_size < 4096 * 4 * 2) {
    init_buffer_size = 4096 * 4 * 2;
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

 private:
  void InternalProcessEnter();
  void InternalProcessExit();
  void InternalProcess(uintptr_t event);
  void ExternalProcessEnter(const std::string& text);
  void ExternalProcessExit(const std::string& text);
  void ExternalProcess(uintptr_t event, const std::string& text);

  MmapWriter mw_;
};

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
  mw_.SetExtendSize(get_extend_buffer_size());
  size_t buffer_size = 4096 * 4;  // used only for last extend
  if (offset >= 0) {
    buffer_size = get_init_buffer_size();
  }
  bool ret = mw_.Open(filename, buffer_size, offset);
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
  } else {
    mw_.Flush(4096 * 4);
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

// #include <dlfcn.h>
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
    logger.Finalize();
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
    logger.Finalize();
    // printf("[call after tracer destructor][ exit][%d]: %p calls %p\n", tid, call_site, func_address);
  }
  // const char* func_name = addr2name(func_address);
  // if (func_name) {
  // printf("[trace func][exit ]:%s\n", func_name);
  // }
}
