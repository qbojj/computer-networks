#pragma once

#include "utils.hpp"

#include <arpa/inet.h>

#include <cstdint>
#include <limits>

namespace addr {
static constexpr std::uint8_t distance_infinity = 16;
static constexpr std::uint8_t keepalive = 3;
static constexpr std::uint8_t dead_broadcast = 3;

#pragma pack(push, 1)
struct net_info {
  std::uint32_t addr;
  std::uint8_t mask_len;
  std::uint32_t distance;
};
#pragma pack(pop)

struct route_info {
  net_info net;
  std::uint32_t next_hop; // 0.0.0.0 if direct connection
  std::uint8_t dead_for{0};

  void kill() {
    if (is_dead())
      return;
    
    net.distance = std::numeric_limits<std::uint32_t>::max();
    dead_for = 0;
  }

  bool is_dead() const {
    return net.distance == std::numeric_limits<std::uint32_t>::max();
  }
};

inline std::uint32_t netmask(std::uint8_t mask_len) {
  return htonl(mask_len == 0 ? 0u : ~ std::uint32_t{0} << (32 - mask_len));
}

utils::handle create_inbound_socket(std::uint16_t port);
} // namespace addr