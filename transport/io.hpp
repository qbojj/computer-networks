#pragma once

#include <vector>
#include <utility>
#include <fstream>
#include <cstdint>

namespace io {
struct input_data {
  std::uint32_t ip;
  std::uint16_t port;
  std::ofstream file;
  std::size_t size;
};

input_data parse_input(int argc, char *argv[]);
} // namespace io