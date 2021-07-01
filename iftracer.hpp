#ifndef IFTRACER_HPP_INCLUDED
#define IFTRACER_HPP_INCLUDED

#include <cassert>
#include <string>

namespace iftracer {
#ifdef IFTRACER_ENABLE_API
void ExternalProcessEnter(const std::string& text);
void ExternalProcessExit(const std::string& text);
#else
void ExternalProcessEnter(const std::string& text) {
  // do nothing used only for passing build
}
void ExternalProcessExit(const std::string& text) {
  // do nothing used only for passing build
}
#endif

class ScopeLogger {
 public:
  // non copyable
  ScopeLogger& operator=(const ScopeLogger&) = delete;

  __attribute__((no_instrument_function)) ScopeLogger() {}
  __attribute__((no_instrument_function)) explicit ScopeLogger(
      const std::string& text) {
    Enter(text);
  }
  __attribute__((no_instrument_function)) ~ScopeLogger() {
    if (entered_flag_) {
      Exit();
    }
  }

  __attribute__((no_instrument_function)) void Enter(const std::string& text) {
    assert(!entered_flag_);
    entered_flag_ = true;
    text_ = text;
    iftracer::ExternalProcessEnter(text);
  }

  __attribute__((no_instrument_function)) void Exit() {
    assert(entered_flag_);
    iftracer::ExternalProcessExit(text_);
    entered_flag_ = false;
  }

 private:
  std::string text_;
  bool entered_flag_ = false;
};
}

#endif  // IFTRACER_HPP_INCLUDED
