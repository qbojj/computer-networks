#pragma once

#include "addrinfo.hpp"

#include <vector>

namespace io {
std::vector<addr::net_info> parse_input();
void print_route(const addr::route_info &ri);
} // namespace io