#include "addrinfo.hpp"

#include <sys/socket.h>
#include <netinet/in.h>

#include <cstdint>

using namespace addr;

utils::handle addr::create_inbound_socket(std::uint16_t port) {
  utils::handle h{ socket(AF_INET, SOCK_DGRAM, 0) };
  if (h < 0)
    utils::throw_sys_error("socket");
  
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(h, (sockaddr*)&addr, sizeof(addr)) < 0)
    utils::throw_sys_error("bind");
  
  int broadcastEnable=1;
  if (setsockopt(h, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0)
    utils::throw_sys_error("setsockopt");

  return h;
}
