/*
Jakub Janeczko, 337670
*/

#include "addrinfo.hpp"
#include "io.hpp"
#include "utils.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <map>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <vector>

typedef std::map<std::pair<std::uint32_t, std::uint8_t>, addr::route_info>
    route_map;
typedef std::map<std::uint32_t, std::uint8_t> next_hop_last_ping_map;
typedef std::vector<std::pair<addr::net_info, bool>> direct_routes_vec;

void update_direct_route(route_map &routes,
                         std::pair<addr::net_info, bool> direct_route) {
  auto [ni, alive] = direct_route;

  if (auto it = routes.find(
          std::pair{ni.addr & addr::netmask(ni.mask_len), ni.mask_len});
      it != routes.end()) {
    
    if (!alive && it->second.next_hop == 0) {
      it->second.kill();
      return;
    }

    if (!alive)
      return;

    if (alive && ntohl(ni.distance) > ntohl(it->second.net.distance))
      return;
    
    // do not allow to update a dead route that is not yet broadcasted
    if (it->second.is_dead() && it->second.dead_for < addr::dead_broadcast)
      return;

    it->second.net.distance = ni.distance;
    it->second.next_hop = 0;
    it->second.dead_for = 0;
    return;
  } else {
    // no entry for direct route -> add it
    std::pair p{ni.addr & addr::netmask(ni.mask_len), ni.mask_len};
    addr::route_info ri{
        .net = {.addr = ni.addr & addr::netmask(ni.mask_len),
                .mask_len = ni.mask_len,
                .distance = alive ? ni.distance
                                  : std::numeric_limits<std::uint32_t>::max()},
        .next_hop = 0,
        .dead_for = 0,
    };

    routes.insert({p, ri});
  }
}

void recieve_packets(int sock, direct_routes_vec &direct_routes,
                     route_map &routes, next_hop_last_ping_map &last_ping,
                     auto until) {
  while (std::chrono::system_clock::now() < until) {
    pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
                       until - std::chrono::system_clock::now())
                       .count();

    timeout = timeout < 0 ? 0 : timeout;
    int poll_res;

    do {
      poll_res = poll(&pfd, 1, timeout);
    } while (poll_res < 0 && errno == EINTR);

    if (poll_res < 0)
      utils::throw_sys_error("poll");

    if (poll_res == 0)
      continue;

    sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    std::byte buffer[IP_MAXPACKET + 1];

    ssize_t len;
    do {
      len = recvfrom(sock, buffer, IP_MAXPACKET, MSG_DONTWAIT,
                     (sockaddr *)&sender, &sender_len);
      // check for EAGAIN / EWOULDBLOCK
      if (len < 0) {
        if (errno == EWOULDBLOCK)
          break;

        if (errno != EAGAIN)
          utils::throw_sys_error("recvfrom");
      }
    } while (len < 0);

    if (len < 0) // WOULDBLOCK
      continue;

    if (len != sizeof(addr::net_info))
      continue;

    addr::net_info *ni = reinterpret_cast<addr::net_info *>(buffer);

    std::uint32_t sender_dist = std::numeric_limits<std::uint32_t>::max();
    bool from_self = false;

    for (auto &dir_route : direct_routes) {
      auto &[ni, alive] = dir_route;

      if (ni.addr == sender.sin_addr.s_addr) {
        // we have sent this packet
        from_self = true;
        break;
      }

      if ((ni.addr & addr::netmask(ni.mask_len)) ==
          (sender.sin_addr.s_addr & addr::netmask(ni.mask_len))) {
        sender_dist = ntohl(ni.distance);
        alive = true;
        break;
      }
    }

    if (from_self)
      continue;

    if (sender_dist == std::numeric_limits<std::uint32_t>::max()) {
      std::cerr << "Received packet from unknown network\n";
      continue;
    }

    last_ping.insert_or_assign(sender.sin_addr.s_addr, 0);

    std::uint32_t packet_dist = ntohl(ni->distance);

    if (packet_dist != std::numeric_limits<std::uint32_t>::max())
      packet_dist += sender_dist;

    ni->distance = htonl(packet_dist);

    auto it =
        routes.find({ni->addr & addr::netmask(ni->mask_len), ni->mask_len});

    if (packet_dist >= addr::distance_infinity) {
      // kill this route (if it really goes through the sender)
      if (it != routes.end() && it->second.next_hop == sender.sin_addr.s_addr) {
        it->second.kill();
      }

      continue;
    }

    // add routes to the routing table
    if (it != routes.end()) {
      // update if the new route is better
      //  but if the current route is dead, wait for the old dead route to
      //  expire
      if ((!it->second.is_dead() ||
           it->second.dead_for >= addr::dead_broadcast) &&
          ntohl(it->second.net.distance) > packet_dist) {
        it->second.net.distance = ni->distance;
        it->second.next_hop = sender.sin_addr.s_addr;
      }
    } else {
      addr::route_info ri{
          .net = *ni,
          .next_hop = sender.sin_addr.s_addr,
      };
      routes.insert(
          {{ni->addr & addr::netmask(ni->mask_len), ni->mask_len}, ri});
    }
  }
}

void kill_stale_routes(route_map &routes,
                       next_hop_last_ping_map &next_hop_last_ping,
                       direct_routes_vec &direct_routes) {
  std::vector<std::uint32_t> to_erase;
  for (auto &[addr, last_ping] : next_hop_last_ping)
    if (last_ping++ >= addr::keepalive)
      to_erase.push_back(addr);

  for (std::uint32_t addr : to_erase) {
    for (auto &ri : routes)
      if (ri.second.next_hop == addr)
        ri.second.kill();

    next_hop_last_ping.erase(addr);
  }

  // remove all dead routes that are not directly connected (after the broadcast
  // dead time has passed)
  std::erase_if(routes, [](const auto &ri) {
    return ri.second.is_dead() && ri.second.next_hop != 0 &&
           ri.second.dead_for >= addr::dead_broadcast;
  });

  for (auto &ri : routes)
    if (ri.second.is_dead())
      if (ri.second.dead_for < addr::dead_broadcast)
        ri.second.dead_for++;

  for (auto &dir_route : direct_routes)
    update_direct_route(routes, dir_route);
}

// send updated routes to all (directly connected) networks
void send_updated_routes(int sock, route_map &routes,
                         direct_routes_vec &direct_routes) {
  for (auto &dir_route : direct_routes) {
    auto &[ni, alive] = dir_route;

    sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(54321);
    dest.sin_addr.s_addr = ni.addr | ~addr::netmask(ni.mask_len);

    alive = sendto(sock, nullptr, 0, 0, (sockaddr *)&dest, sizeof(dest)) >= 0;

    for (auto &r : routes) {
      if (!alive)
        break;

      // only possible for directly connected networks
      if (r.second.is_dead() && r.second.dead_for >= addr::dead_broadcast)
        continue;

      if (sendto(sock, &r.second.net, sizeof(r.second.net), 0,
                 (sockaddr *)&dest, sizeof(dest)) < 0)
        alive = false;
    }

    update_direct_route(routes, dir_route);
  }
}

int main() {
  auto connections = io::parse_input();
  auto sock = addr::create_inbound_socket(54321);

  route_map routes;
  direct_routes_vec direct_routes;
  next_hop_last_ping_map next_hop_last_ping;

  for (auto &c : connections) {
    addr::route_info ri{};
    ri.net = c;
    ri.net.addr &= addr::netmask(ri.net.mask_len);
    routes.insert({{ri.net.addr, ri.net.mask_len}, ri});

    direct_routes.push_back({c, true});
  }

  while (true) {
    using namespace std::chrono_literals;
    recieve_packets(sock, direct_routes, routes, next_hop_last_ping,
                    std::chrono::system_clock::now() + 15s);
    kill_stale_routes(routes, next_hop_last_ping, direct_routes);
    send_updated_routes(sock, routes, direct_routes);

    std::cout << "Routing table:\n";
    for (auto &r : routes)
      io::print_route(r.second);
  }
}
