#include "io.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace io;

std::vector<addr::net_info> io::parse_input() {
  std::vector<addr::net_info> connections;

  std::string line;
  std::getline(std::cin, line);

  std::size_t pos;
  int n = std::stoi(line, &pos);
  if (n < 0) {
    throw std::invalid_argument("Invalid number of connections");
  }

  // check if nothing more in the line (only whitespaces)
  if (std::ranges::any_of(line.substr(pos),
                          [](char c) { return !std::isspace(c); })) {
    throw std::invalid_argument("Invalid input format");
  }

  for (int i = 0; i < n; i++) {
    std::getline(std::cin, line);

    // validate line format:
    // <ipv4>/<mask_len> "distance" <distance>

    int addr_parts[4];
    int mask_len, distance;

    if (std::sscanf(line.c_str(), "%d.%d.%d.%d/%d distance %d", &addr_parts[0],
                    &addr_parts[1], &addr_parts[2], &addr_parts[3], &mask_len,
                    &distance) != 6) {
      throw std::invalid_argument("Invalid input format");
    }

    // validate address parts
    for (int j = 0; j < 4; j++) {
      if (addr_parts[j] < 0 || addr_parts[j] > 255) {
        throw std::invalid_argument("Invalid address part");
      }
    }

    // validate mask_len
    if (mask_len < 0 || mask_len > 32) {
      throw std::invalid_argument("Invalid mask length");
    }

    // validate distance
    if (distance <= 0 || distance >= addr::distance_infinity) {
      throw std::invalid_argument("Invalid distance");
    }

    // store the parsed data
    connections.push_back(addr::net_info{
        .addr =
            htonl((static_cast<std::uint32_t>(addr_parts[0]) << 24) |
                  (addr_parts[1] << 16) | (addr_parts[2] << 8) | addr_parts[3]),
        .mask_len = static_cast<std::uint8_t>(mask_len),
        .distance = htonl(static_cast<std::uint32_t>(distance)),
    });
  }

  return connections;
}

void io::print_route(const addr::route_info &ri) {
  std::cout << utils::ip_to_string(ri.net.addr) << "/" << std::uint32_t{ri.net.mask_len} << " ";
  
  if (ri.is_dead())
    std::cout << "unreachable ";
  else
    std::cout << "distance " << ntohl(ri.net.distance) << " ";

  if (!ri.next_hop)
    std::cout << "connected directly";
  else
    std::cout << "via " << utils::ip_to_string(ri.next_hop);
  
  std::cout << std::endl;
}
