#pragma once

#include <vector>
#include <utility>
#include <fstream>
#include <cstdint>
#include <filesystem>

namespace io {
struct input_data {
  std::uint16_t port;
  std::filesystem::path directory;
};

input_data parse_input(int argc, char *argv[]);
} // namespace io