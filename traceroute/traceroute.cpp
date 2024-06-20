/*
Jakub Janeczko, 337670
*/

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <ranges>
#include <stdexcept>

[[noreturn]] void throw_sys_error(const char *msg) {
  throw std::system_error(errno, std::system_category(), msg);
}

template <std::size_t N>
std::uint16_t comp_checksum(std::span<const std::byte, N> buf) {
  std::uint32_t sum = 0;

  std::span<const std::uint16_t> words = {
      reinterpret_cast<const std::uint16_t *>(buf.data()), buf.size() / 2};

  for (std::uint16_t word : words)
    sum += ntohs(word);

  if (buf.size() % 2 != 0) {
    std::uint8_t data[2] = {std::bit_cast<std::uint8_t>(buf.back()), 0};
    sum += ntohs(*reinterpret_cast<std::uint16_t *>(data));
  }

  sum = (sum >> 16) + (sum & 0xFFFF);
  return htons(static_cast<std::uint16_t>(~(sum + (sum >> 16))));
}

// used to identify packet in response
struct packet_info {
  bool arrived;

  std::uint16_t seq;
  std::chrono::duration<double, std::milli> millis;
  in_addr ip;
  bool done;
};

// RAII wrapper for file descriptor
class handle {
public:
  handle() : h(-1) {}
  explicit handle(int h) : h(h) {}
  handle(const handle &) = delete;
  handle(handle &&other) noexcept : h(other.h) { other.h = -1; }
  handle &operator=(handle other) noexcept {
    std::swap(h, other.h);
    return *this;
  }
  ~handle() {
    if (h != -1)
      close(h);
  }

  operator int() const { return h; }

private:
  int h;
};

void send_packets(std::span<packet_info> infos, std::uint16_t id,
                  std::uint16_t &seq, in_addr target, int sock) {
  // prepare packet
  icmp header[1];
  std::memset(&header, 0, sizeof(header));
  header->icmp_type = ICMP_ECHO;
  header->icmp_code = 0;
  header->icmp_id = id;

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr = target;

  for (packet_info &pi : infos) {
    std::memset(&pi, 0, sizeof(pi));
    pi.seq = header->icmp_seq = seq++;
    header->icmp_cksum = 0;

    std::span header_bytes = std::as_bytes(std::span{header});
    header->icmp_cksum = comp_checksum(header_bytes);
    assert(comp_checksum(header_bytes) == 0);

    ssize_t res = sendto(sock, header, sizeof(header), 0,
                         reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    if (res < 0)
      throw_sys_error("sendto");
  }
}

bool poll_in(int sock, int timeout = 50) {
  pollfd pfd;
  pfd.fd = sock;
  pfd.events = POLLIN;
  pfd.revents = 0;
  int poll_res = poll(&pfd, 1, timeout);
  if (poll_res < 0)
    throw_sys_error("poll");

  if (poll_res == 0)
    return false;

  if (pfd.revents & POLLHUP)
    throw std::runtime_error("connection closed\n");

  if (pfd.revents & POLLIN)
    return true;

  return false;
}

template <typename Clock = std::chrono::steady_clock>
bool handle_packet(std::span<packet_info> infos, sockaddr_in &sender,
                   std::byte *buffer, auto start, std::uint16_t my_id) {

  ip *ip_header = reinterpret_cast<ip *>(buffer);
  ssize_t ip_header_len = 4 * ip_header->ip_hl;

  // check if it is ICMP packet
  if (ip_header->ip_p != IPPROTO_ICMP)
    return false;

  icmp *icmp_header = reinterpret_cast<icmp *>(buffer + ip_header_len);

  // TTL exceeded or echo reply
  if (icmp_header->icmp_type != ICMP_TIME_EXCEEDED &&
      icmp_header->icmp_type != ICMP_ECHOREPLY)
    return false;

  // check if it is response to our packet
  if (icmp_header->icmp_type == ICMP_TIME_EXCEEDED) {
    // get to the ID of the original message
    //  (payload has the original packet)

    buffer = reinterpret_cast<std::byte *>(icmp_header->icmp_data);

    ip_header = reinterpret_cast<ip *>(buffer);
    if (ip_header->ip_p != IPPROTO_ICMP)
      return false;
    ip_header_len = 4 * ip_header->ip_hl;

    icmp_header = reinterpret_cast<icmp *>(buffer + ip_header_len);
    if (icmp_header->icmp_type != ICMP_ECHO)
      return false;
  }

  assert(icmp_header->icmp_type == ICMP_ECHOREPLY ||
         icmp_header->icmp_type == ICMP_ECHO);

  if (icmp_header->icmp_id != my_id)
    return false;

  for (packet_info &pi : infos) {
    if (icmp_header->icmp_seq != pi.seq)
      continue;

    assert(!pi.arrived);

    pi = {.arrived = true,
          .seq = icmp_header->icmp_seq,
          .millis = Clock::now() - start,
          .ip = sender.sin_addr,
          .done = icmp_header->icmp_type == ICMP_ECHOREPLY};

    return true;
  }

  return false;
}

template <typename Clock = std::chrono::steady_clock>
int pull_all_packets(std::span<packet_info> infos, auto start, int sock,
                     std::uint16_t my_id) {
  int recieved = 0;
  while (true) {
    sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    std::byte buffer[IP_MAXPACKET];
    ssize_t packet_len =
        recvfrom(sock, buffer, IP_MAXPACKET, MSG_DONTWAIT,
                 reinterpret_cast<sockaddr *>(&sender), &sender_len);

    if (packet_len < 0) {
      if (errno == EWOULDBLOCK)
        return recieved;

      if (errno == EAGAIN)
        continue;

      throw_sys_error("recvfrom");
    }

    recieved += handle_packet<Clock>(infos, sender, buffer, start, my_id);
  }
}

template <typename Clock = std::chrono::steady_clock>
void wait_for_packets(int sock, std::span<packet_info> infos, auto timeout,
                      std::uint16_t my_id) {
  std::size_t recieved = 0;
  auto start = Clock::now();

  // wait for responses (with timeout)
  while (Clock::now() - start < timeout && recieved < infos.size()) {
    if (poll_in(sock))
      recieved += pull_all_packets<Clock>(infos, start, sock, my_id);
  }
}

void print_status(std::span<const packet_info> infos) {
  std::size_t recieved = 0;
  double sum_millis = 0;

  auto arrived_packets = infos | std::views::filter(&packet_info::arrived);
  for (const packet_info &pi : arrived_packets) {
    recieved++;
    sum_millis += pi.millis.count();

    auto diff_addr = [&pi](const auto &pi2) {
      return pi2.ip.s_addr != pi.ip.s_addr;
    };

    if (std::ranges::all_of(arrived_packets | std::views::take(recieved - 1),
                            diff_addr))
      std::cout << inet_ntoa(pi.ip) << " ";
  }

  if (recieved == infos.size()) {
    std::cout << std::lround(sum_millis / recieved) << "ms\n";
  } else if (recieved == 0) {
    std::cout << "*\n";
  } else {
    std::cout << "???\n";
  }

  std::cout.flush();
}

std::pair<handle, in_addr> init(const char *str) {
  in_addr target;
  if (int res = inet_pton(AF_INET, str, &target); res != 1) {
    if (res < 0)
      throw_sys_error("inet_pton");
    else
      throw std::runtime_error(std::string("Invalid address: ") + str);
  }

  // open socket
  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock < 0)
    throw_sys_error("socket");

  return std::make_pair(handle{sock}, target);
}

int main(int argc, char *argv[]) try {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <host>\n";
    return 1;
  }

  auto [sock, target] = init(argv[1]);
  std::uint16_t my_id = getpid() & 0xFFFF;
  std::uint16_t seq = 1;

  for (int ttl = 1; ttl <= 30; ++ttl) {
    std::cout << ttl << ". ";

    // set ttl
    setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

    std::array<packet_info, 3> infos;
    infos.fill({});

    send_packets(infos, my_id, seq, target, sock);
    wait_for_packets(sock, infos, std::chrono::seconds(1), my_id);

    print_status(infos);

    if (std::ranges::all_of(infos, &packet_info::done))
      break;
  }

  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}