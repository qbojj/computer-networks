#include "io.hpp"
#include "utils.hpp"

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <chrono>
#include <span>
#include <string>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

// max buffer size (we have 6MB for the whole program, so 1MB for the buffer is
// reasonable)
static constexpr std::size_t max_memory = 1 * 1024 * 1024;
static constexpr std::size_t block_max_size = 1000;

static constexpr std::size_t max_blocks = max_memory / block_max_size;

struct block_t {
  std::span<std::byte> as_span() { return {data.data(), size}; }
  std::span<const std::byte> as_span() const { return {data.data(), size}; }

  block_t() = default;
  block_t(std::size_t begin, std::size_t size)
      : begin(begin), size(size),
        last_request(std::chrono::steady_clock::now()){};

  std::array<std::byte, block_max_size> data{};
  std::size_t begin{0};
  std::size_t size{0};
  bool downloaded{false};

  std::chrono::time_point<std::chrono::steady_clock> last_request{};
};

std::string get_get_message(std::size_t begin, std::size_t size) {
  return "GET " + std::to_string(begin) + " " + std::to_string(size) + "\n";
}

void request_packet(int sock, block_t &block, std::size_t begin,
                    std::size_t size) {
  std::string send_msg = get_get_message(begin, size);

  if (send(sock, send_msg.data(), send_msg.size(), 0) < 0)
    utils::throw_sys_error("sendto");

  block = block_t(begin, size);
}

bool wait_for_message(int sock, auto until) {
  pollfd pfd;
  pfd.fd = sock;
  pfd.events = POLLIN;
  pfd.revents = 0;

  int res;
  auto now = std::chrono::steady_clock::now();

  while (now < until) {
    now = std::chrono::steady_clock::now();
    auto dt =
        std::chrono::duration_cast<std::chrono::milliseconds>(until - now);
    res = poll(&pfd, 1, dt.count());

    if (res < 0) {
      if (errno == EAGAIN)
        continue;

      utils::throw_sys_error("poll");
    }

    return res > 0;
  }

  return false;
}

void handle_packet(std::span<block_t> blocks, std::span<const std::byte> packet)
{
  std::string_view packet_str{reinterpret_cast<const char *>(packet.data()), packet.size()};
  std::size_t begin, size;

  std::string header{packet_str.substr(0, packet_str.find('\n'))};
  
  if (sscanf(header.data(), "DATA %zu %zu\n", &begin, &size) != 2)
    return; // invalid packet
  
  int pos = header.size() + 1;

  // find the block that starts at the given position
  //auto it = std::ranges::find(blocks, begin, &block_t::begin);
  auto it = std::find_if(blocks.begin(), blocks.end(), [begin](const block_t &block) { return block.begin == begin; });
  if (it == blocks.end())
    return; // block not found (probably already committed)
  
  if (it->downloaded)
    return; // block already downloaded (probably a duplicate packet)
  
  if (size != it->size)
    return; // invalid size
  
  if (packet.size() - pos != size)
    return; // invalid size (packet size does not match the size in the packet)

  // copy the data to the block
  std::span<const std::byte> data{packet.data() + pos, size};

  //std::ranges::copy(data, it->as_span().begin());
  std::copy(data.begin(), data.end(), it->as_span().begin());
  it->downloaded = true;
}

void recieve_packets(int sock, std::span<block_t> blocks)
{
  do {
    std::array<std::byte, block_max_size + 512> buffer;
    int res = recv(sock, buffer.data(), buffer.size(), MSG_DONTWAIT);

    if (res < 0) {
      if (errno == EWOULDBLOCK)
        break;
      
      utils::throw_sys_error("recv");
    }
    
    if (res == 0)
      throw std::runtime_error("Connection closed");
    
    handle_packet(blocks, {buffer.data(), static_cast<std::size_t>(res)});
  } while (true);
}

int main(int argc, char *argv[]) {
  auto [ip, port, file, size] = io::parse_input(argc, argv);

  utils::handle sock(socket(AF_INET, SOCK_DGRAM, 0));
  if (sock == -1)
    utils::throw_sys_error("socket");

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ip;
  addr.sin_port = port;

  // connect to the server so we do not have to specify the address in sendto
  //     and make recvfrom only receive from the connected address
  if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    utils::throw_sys_error("connect");

  // check if the max size of packet is OK

  int block_size_;
  socklen_t block_size_len = sizeof(block_size_);
  if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &block_size_, &block_size_len) < 0)
    utils::throw_sys_error("getsockopt");
  
  if (block_size_ < 30)
    throw std::runtime_error("Minimum size of packet is too small");
  
  std::size_t block_size = std::min(static_cast<std::size_t>(block_size_ - 30), block_max_size);

  std::size_t window_size =
      std::min((size + block_size - 1) / block_size, max_blocks);
  std::vector<block_t> blocks(window_size);

  std::size_t to_commit = 0; // next block to commit
  for (std::size_t i = 0; i < window_size; ++i) {
    std::size_t sz = std::min(static_cast<std::size_t>(block_size), size - i * block_size);
    request_packet(sock, blocks[i], i * block_size, sz);
  }

  double last_percentage_printed = -10000000;
  
  std::size_t bytes_saved = 0;
  while (bytes_saved < size) {

    // percentage of the file downloaded
    double percentage = static_cast<double>(bytes_saved) / size * 100;
    if (percentage - last_percentage_printed > 0.1) {
      std::printf("%.2f%%\n", percentage);
      last_percentage_printed = percentage;
    }

    using namespace std::chrono_literals;
    auto now = std::chrono::steady_clock::now();

    bool recieved = wait_for_message(sock, now + 500ms);
    if (recieved) recieve_packets(sock, blocks);

    // commit the blocks at the beginning of the window
    while (blocks[to_commit].downloaded) {
      std::span<const std::byte> data = blocks[to_commit].as_span();
      file.write(reinterpret_cast<const char *>(data.data()), data.size());
      bytes_saved += data.size();

      if (bytes_saved >= size)
        break;

      blocks[to_commit].downloaded = false;

      std::size_t next_beg = bytes_saved + window_size * block_size;
      if (next_beg < size) {
        std::size_t sz = std::min(static_cast<std::size_t>(block_size), size - next_beg);
        request_packet(sock, blocks[to_commit], next_beg, sz);
      }

      to_commit = (to_commit + 1) % window_size;
    }

    if (bytes_saved >= size)
      break;
    
    now = std::chrono::steady_clock::now();
    
    // re-request the blocks that were not downloaded if the last request was too long ago
    for (auto &block : blocks) {
      if (!block.downloaded && now - block.last_request > 500ms) {
        request_packet(sock, block, block.begin, block.size);
      }
    }
  }
}