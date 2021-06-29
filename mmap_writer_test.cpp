#include <algorithm>
#include <cassert>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <vector>

#include <string.h>

#include "mmap_writer.hpp"

int main(const int argc, const char* argv[]) {
  bool ret                = false;
  size_t init_buffer_size = 4096 * 100;
  size_t extend_size      = 4096 * 4;
  size_t offset           = 0;

  int unit_size = 100;
  int loop_num  = 1000;

  std::string filename = "mmap_writer_test.bin";
  std::vector<uint8_t> expected_data(unit_size * loop_num);

  MmapWriter mw;
  mw.SetExtendSize(extend_size);
  ret = mw.Open(filename, init_buffer_size, offset);
  if (!ret) {
    std::cerr << mw.GetErrorMessage() << std::endl;
    return 1;
  }
  for (int i = 0; i < loop_num; i++) {
    ret = mw.PrepareWrite(unit_size);
    if (!ret) {
      std::cerr << mw.GetErrorMessage() << std::endl;
      break;
    }
    memset(mw.cursor_, i % 256, unit_size);
    memset(&expected_data[unit_size * i], i % 256, unit_size);
    mw.Seek(unit_size);
    mw.Flush(4096);
  }
  ret = mw.Close();
  if (!ret) {
    std::cerr << mw.GetErrorMessage() << std::endl;
    return 1;
  }

  std::ifstream result_file(filename, std::ios::in | std::ios::binary);
  std::vector<uint8_t> result_data(
      (std::istreambuf_iterator<char>(result_file)),
      std::istreambuf_iterator<char>());
  if (result_data.size() != expected_data.size()) {
    std::cout << "wrong file size: expected: " << expected_data.size()
              << ", got: " << result_data.size() << std::endl;
    return 1;
  }
  for (size_t i = 0; i < expected_data.size(); i++) {
    assert(result_data[i] == expected_data[i] || !"wrong data");
  }
  return 0;
}
