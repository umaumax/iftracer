#include "mmap_writer.hpp"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstring>
#include <string>

bool MmapWriter::IsOpen() { return is_open_; }

// offset == 0: truncate file
// offset  < 0: seek to last offset, extend size
bool MmapWriter::Open(std::string filename, size_t size, int64_t offset = 0) {
  if (debug_ && IsOpen()) {
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

  aligned_file_size_ = PAGE_ALIGNED(size);
  map_size_          = PAGE_ALIGNED(aligned_file_size_ - offset);

  if (ftruncate(fd_, aligned_file_size_) != 0) {
    AddErrorMessageWithErrono("Open(): ftruncate():", errno);
    return false;
  }

  size_t aligned_offset = PAGE_ALIGNED_ROUND_DOWN(offset);

  if (verbose_) {
    printf("[Open]\n");
    printf("offset:%" PRId64 "\n", offset);
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
  if (debug_ && !IsOpen()) {
    AddErrorMessage("Close(): no opening map:");
    return false;
  }
  if (verbose_) {
    printf("[Close]\n");
    printf("file_offset_:%zu\n", file_offset_);
    printf("local_offset_:%zu\n", local_offset_);
    printf("map_size_:%zu\n", map_size_);
  }

  // if (local_offset_ != 0) {
  // if (msync(head_, local_offset_, MS_SYNC) != 0) {
  // AddErrorMessageWithErrono("Close(): msync():", errno);
  // return false;
  // }
  // }
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

bool MmapWriter::Flush(size_t size) {
  if (local_offset_ < size) {
    return false;
  }
  size_t aligned_size = PAGE_ALIGNED_ROUND_DOWN(size);
  if (aligned_size == 0) {
    return false;
  }
  if (debug_ && !IsOpen()) {
    AddErrorMessage("Flush(): no opening map:");
    return false;
  }
  if (verbose_) {
    printf("[Flush]\n");
    printf("aligned_size:%zu\n", aligned_size);
  }
  // if (msync(head_, aligned_size, MS_SYNC) != 0) {
  // AddErrorMessageWithErrono("Flush(): msync():", errno);
  // return false;
  // }
  if (munmap(head_, aligned_size) != 0) {
    AddErrorMessageWithErrono("Flush(): munmap():", errno);
    return false;
  }

  head_ += aligned_size;
  map_size_ -= aligned_size;
  local_offset_ -= aligned_size;
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
    extend_size = PAGE_ALIGNED(size);
  }
  size_t new_file_size = aligned_file_size_ + extend_size;
  size_t new_offset =
      PAGE_ALIGNED_ROUND_DOWN(file_offset_) + (local_offset_ % PAGE_SIZE);
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

void MmapWriter::AddErrorMessage(std::string message) {
  error_message_ = message + error_message_;
}
void MmapWriter::AddErrorMessageWithErrono(std::string message,
                                           int errno_value) {
  error_message_ =
      message + std::string(std::strerror(errno)) + ":" + error_message_;
}
