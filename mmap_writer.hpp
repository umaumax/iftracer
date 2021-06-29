#ifndef MMAP_WRITER_HPP_INCLUDED
#define MMAP_WRITER_HPP_INCLUDED

#include <unistd.h>

#include <cstdint>
#include <string>

class MmapWriter {
 public:
  MmapWriter(){};
  ~MmapWriter() {
    if (IsOpen()) {
      Close();
    }
  };
  void SetExtendSize(size_t extend_size) { extend_size_ = extend_size; };
  bool IsOpen();
  bool Open(std::string filename, size_t size, int64_t offset);
  bool Close();
  bool Flush(size_t size);
  bool CheckCapacity(size_t size);
  bool PrepareWrite(size_t size);
  void Seek(size_t n);
  std::string GetErrorMessage();
  uint8_t* Cursor();

  // private:
  void AddErrorMessage(std::string message);
  void AddErrorMessageWithErrono(std::string message, int errno_value);

  const bool verbose_ = false;
#ifdef IFTRACE_DEBUG
  const bool debug_ = true;
#else
  const bool debug_ = false;
#endif
  size_t extend_size_ = 4096 * 4;

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

  // NOTE: below value is declared as field for initialize this class constructor timing
  const size_t PAGE_SIZE = getpagesize();
  const size_t PAGE_MASK = (PAGE_SIZE - 1);

  size_t PAGE_ALIGNED(size_t x) { return (((x) + PAGE_MASK) & ~PAGE_MASK); }
  size_t PAGE_ALIGNED_ROUND_DOWN(size_t x) { return ((x) & ~PAGE_MASK); }
};

#endif  // MMAP_WRITER_HPP_INCLUDED
