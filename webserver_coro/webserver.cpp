#include "httpInfo.hpp"
#include "io.hpp"
#include "io_engine.hpp"
#include "utils.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <random>
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

coro::lazy_task<bool> handle_request(
  coro::io_engine &engine,
  const utils::handle &sock,
  std::string_view request,
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

  co_await io::send_all(engine, sock, response);
  co_return req.keep_alive;
}

coro::async_generator<char> socket_stream(coro::io_engine &engine,
                                          const utils::handle &sock,
                                          auto timeout_duration) {
  while (true) {
    if (co_await engine.poll_for(sock, POLLIN, timeout_duration) == 0)
      co_return;

    char buffer[1024];
    ssize_t bytes_read = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);

    if (bytes_read < 0) {
      if (errno == EWOULDBLOCK)
        continue;
      utils::throw_sys_error("recv");
    }

    for (int i = 0; i < bytes_read; ++i)
      co_yield buffer[i];
  }
}

coro::task handle_client(coro::io_engine &engine, utils::handle sock,
                         std::filesystem::path directory, int request_id) try {

  if (utils::debug_mode)
    std::cout << "New connection (" << request_id << ") -> fd = " << (int)sock
              << "\n";

  std::string request;

  auto stream = socket_stream(engine, sock, std::chrono::seconds(15));
  auto end = stream.end();
  for (auto it = co_await stream.begin(); it != end; co_await ++it) {
    request.push_back(*it);
    if (request.ends_with("\r\n\r\n")) {
      if (!co_await handle_request(engine, sock, request, directory))
        break;

      request.clear();
    }
  }

  if (utils::debug_mode)
    std::cout << "Connection closed (" << request_id << ")\n";

  co_return;
} catch (const std::exception &e) {
  std::cout << "Exception in handle_client (" << request_id << "): " << e.what()
            << '\n';
} catch (...) {
  std::cout << "Exception in handle_client (" << request_id << '\n';
}

} // namespace

coro::task server_listener(coro::io_engine &engine, int argc,
                           char *argv[]) try {
  io::input_data data = io::parse_input(argc, argv);
  utils::debug_mode = data.debug_mode;

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

  // set no-block
  int flags = fcntl(server_socket, F_GETFL, 0);
  if (flags == -1)
    utils::throw_sys_error("fcntl");

  if (fcntl(server_socket, F_SETFL, flags | O_NONBLOCK) == -1)
    utils::throw_sys_error("fcntl");

  int request_count = 0;

  if (data.inspirational_quotes)
    io::quote_generator(engine, data.quote_interval);

  while (true) {
    co_await engine.poll(server_socket, POLLIN);

    utils::handle client_socket(
        accept4(server_socket, nullptr, nullptr, SOCK_NONBLOCK));
    if (!client_socket) {
      if (errno == EAGAIN)
        continue;
      utils::throw_sys_error("accept4");
    }

    if (utils::debug_mode)
      std::cout << "New connection\n";
    handle_client(engine, std::move(client_socket), data.directory,
                  request_count++);
  }

} catch (const std::exception &e) {
  std::cout << "Exception in server_listener: " << e.what() << '\n';
} catch (...) {
  std::cout << "Exception in server_listener\n";
}

int main(int argc, char *argv[]) {
  coro::io_engine engine;
  server_listener(engine, argc, argv);
  engine.pull_all();
}