#include "httpInfo.hpp"
#include "io.hpp"
#include "utils.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <string_view>

namespace {
http::request get_request_data(std::string_view request,
                               const std::filesystem::path &directory) {
  // request format: <method> <path> <version>\r\n<headers>\r\n
  using namespace std::literals;
  auto lines = std::views::split(request, "\r\n"sv);

  if (std::ranges::distance(lines) <= 2)
    return {};

  auto parts = std::views::split(lines.front(), " "sv);
  if (std::ranges::distance(parts) != 3)
    return {};

  auto to_sv = [](const auto &s) {
    return std::string_view{s.begin(), s.end()};
  };

  auto it = parts.begin();
  std::string_view method = to_sv(*it);
  std::filesystem::path path = to_sv(*++it);
  std::string_view version = to_sv(*++it);

  if (method != "GET")
    return {};

  if (version != "HTTP/1.1")
    return {};

  // file path is at directory/host/path

  auto headers = lines | std::views::drop(1) |
                 std::views::take(std::ranges::distance(lines) - 3) |
                 std::views::transform(to_sv);

  bool keep_alive = true;

  std::string_view host;
  for (auto line : headers) {
    std::size_t pos = line.find(':');
    if (pos == std::string_view::npos)
      return {};

    // strip whitespaces
    auto strip = [](std::string_view sv) {
      while (!sv.empty() && std::isspace(sv.front()))
        sv.remove_prefix(1);
      while (!sv.empty() && std::isspace(sv.back()))
        sv.remove_suffix(1);
      return sv;
    };

    auto key = strip(line.substr(0, pos));
    auto value = strip(line.substr(pos + 1));

    if (key == "Host")
      host = value;

    if (key == "Connection" && value == "close")
      keep_alive = false;
  }

  if (host.empty())
    return {};

  // check if host is valid (no slashes)
  if (host.find('/') != std::string::npos)
    return {};

  // check if path is valid (all ".." should not move above the root directory)
  //  make path a normal form (before that remove leading / to preserve all
  //  leading ..)
  path = path.lexically_proximate("/").lexically_normal();

  // if any .. survived that means we are going above the root
  if (path.native().find("..") != std::string::npos)
    return {http::r403{}, keep_alive}; // forbidden

  std::string_view host_no_port = host.substr(0, host.find_last_of(':'));

  std::filesystem::path file_path = directory / host_no_port / path;

  if (!std::filesystem::exists(file_path))
    return {http::r404{}, keep_alive};

  if (std::filesystem::is_directory(file_path)) {
    auto index_path = (host / path / "index.html").lexically_normal();
    return {http::r301{{}, "http://" + index_path.generic_string()},
            keep_alive}; // permanent redirect
  }

  return {http::r200{{}, file_path}, keep_alive};
}

bool handle_request(const utils::handle &sock, std::string_view request,
                    const std::filesystem::path &directory) {
  std::string response;

  http::request req;

  try {
    req = get_request_data(request, directory);
    response = http::get_response("HTTP/1.1", req);
  } catch (...) {
    // send internal server error instead
    req = {http::r500{}};
    response = http::get_response("HTTP/1.1", req);
  }

  utils::send_all(sock, response);
  return req.keep_alive;
}

void handle_client(const utils::handle &sock,
                   const std::filesystem::path &directory) {
  // wait for HTTP request (close connection after 1s of inactivity)

  auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(1);

  // holds unconsumed data
  std::string buffer_since_last;

  while (std::chrono::steady_clock::now() < timeout) {
    // use poll()

    pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, 1000);
    if (ret == -1)
      utils::throw_sys_error("poll");

    if (ret == 0)
      continue;

    // is connection closed?
    if (pfd.revents & POLLHUP)
      return;

    // read data

    ssize_t bytes_read;

    do {
      char buffer[1024];
      bytes_read = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);

      // if EINTR, try again
      if (bytes_read < 0) {
        if (errno == EINTR)
          continue;
        utils::throw_sys_error("recv");
      }

      // connection closed
      if (bytes_read == 0)
        return;

      // process data
      buffer_since_last.append(buffer, bytes_read);
      timeout = std::chrono::steady_clock::now() + std::chrono::seconds(1);

      // check if we have a full request
      auto pos = buffer_since_last.find("\r\n\r\n");

      if (pos != std::string::npos) {
        std::string_view request(buffer_since_last.data(), pos + 4);
        
        if (!handle_request(sock, request, directory))
          return;

        buffer_since_last.erase(0, pos + 4);
      }
    } while (bytes_read > 0);
  }
}
} // namespace

int main(int argc, char *argv[]) {
  io::input_data data = io::parse_input(argc, argv);
  utils::handle server_socket(socket(AF_INET, SOCK_STREAM, 0));

  if (!server_socket)
    utils::throw_sys_error("socket");

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = data.port;
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) <
      0)
    utils::throw_sys_error("bind");

  if (listen(server_socket, 5) < 0)
    utils::throw_sys_error("listen");

  while (true) {
    utils::handle client_socket(accept(server_socket, nullptr, nullptr));
    if (!client_socket)
      utils::throw_sys_error("accept");

    // handle client synchronously (no need to handle multiple clients)
    try {
      handle_client(client_socket, data.directory);
    } catch (...) {
      // ignore
    }
  }
}