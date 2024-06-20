#pragma once

#include <unistd.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <utility>

namespace utils {
inline bool debug_mode = false;

[[noreturn]] void throw_sys_error(const char *msg);
std::exception_ptr make_sys_error(const char *msg);

// RAII wrapper for file descriptor
class handle {
public:
  handle() : h(-1) {}
  ~handle() {
    if (h != -1)
      close(h);
  }
  explicit handle(int h) : h(h) {}

  handle(const handle &) = delete;
  handle(handle &&o) : h(std::exchange(o.h, -1)) {}
  handle &operator=(handle o) {
    std::swap(h, o.h);
    return *this;
  }

  explicit operator bool() const { return h != -1; }
  operator int() const { return h; }

private:
  int h;
};

template <typename... Ts> struct overload : Ts... {
  using Ts::operator()...;
};

[[nodiscard]] std::string ip_to_string(std::uint32_t ip);
} // namespace utils
