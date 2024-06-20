#pragma once

#include "io_engine.hpp"
#include "utils.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace io {
struct input_data {
  std::uint16_t port;
  std::filesystem::path directory;
  bool debug_mode;
  bool inspirational_quotes;
  std::chrono::milliseconds quote_interval;
};

input_data parse_input(int argc, char *argv[]);

coro::eager_task<> send_all(coro::io_engine &engine, const utils::handle &sock, std::span<const std::byte> data);
coro::eager_task<> send_all(coro::io_engine &engine, const utils::handle &sock, std::string_view data);

coro::task quote_generator(coro::io_engine &engine, std::chrono::milliseconds interval);
} // namespace io