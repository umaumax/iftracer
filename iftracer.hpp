#ifndef IFTRACER_HPP_INCLUDED
#define IFTRACER_HPP_INCLUDED

#include <cassert>
#include <string>

namespace iftracer {
#ifdef IFTRACER_ENABLE_API
void ExtendEventDurationEnter();
void ExtendEventDurationExit(const std::string& text);
#else
inline void ExtendEventDurationEnter() {
  // do nothing used only for passing build
}
inline void ExtendEventDurationExit(const std::string& text) {
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

  __attribute__((no_instrument_function)) void Enter() {
    assert(!entered_flag_);
    entered_flag_ = true;
    iftracer::ExtendEventDurationEnter();
  }

  __attribute__((no_instrument_function)) void Enter(const std::string& text) {
    assert(!entered_flag_);
    entered_flag_ = true;
    SetText(text);
    iftracer::ExtendEventDurationEnter();
  }

  __attribute__((no_instrument_function)) void Exit() { Exit(text_); }

  __attribute__((no_instrument_function)) void Exit(const std::string& text) {
    assert(entered_flag_);
    iftracer::ExtendEventDurationExit(text);
    entered_flag_ = false;
  }

  __attribute__((no_instrument_function)) void SetText(
      const std::string& text) {
    text_ = text;
  }

 private:
  bool entered_flag_ = false;
  std::string text_;
};
}  // namespace iftracer

#endif  // IFTRACER_HPP_INCLUDED
