#include "utils.hpp"

#include <arpa/inet.h>

#include <stdexcept>
#include <system_error>
#include <bit>
#include <cstdint>
#include <string>

using namespace utils;

[[noreturn]] void utils::throw_sys_error(const char *msg) {
  throw std::system_error(errno, std::system_category(), msg);
}

[[nodiscard]] std::string utils::ip_to_string(std::uint32_t ip) {
  ip = ntohl(ip);
  std::string result;
  result.reserve(15);
  for (int i = 3; i >= 0; --i) {
    result.append(std::to_string((ip >> (i * 8)) & 0xff));
    if (i > 0) {
      result.push_back('.');
    }
  }
  return result;
}
