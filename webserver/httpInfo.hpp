#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <ranges>
#include <string_view>
#include <variant>

namespace http {
struct mime_type {
  std::string_view extension;
  std::string_view type;
};

const std::array mime_types = std::to_array<mime_type>({
    {".txt", "text/plain; charset=utf-8"},
    {".html", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".pdf", "application/pdf"},
});

namespace detail {
struct http_response {
  int code;
  std::string_view header;
  std::string_view message;
};

const std::array http_responses = std::to_array<http_response>({
    {200, "200 OK", ""},
    {301, "301 Moved Permanently",
     R"(<!DOCTYPE html>
<html>
  <head>
    <title>301 Moved Permanently</title>
  </head>
  <body>
    <h1>Moved Permanently</h1>
    <p>The document has moved <a href="{}">here</a>.</p>
  </body>
</html>)"},
    {403, "403 Forbidden",
          R"(<!DOCTYPE html>
<html>
  <head>
    <title>403 Forbidden</title>
  </head>
  <body>
    <h1>Forbidden</h1>
    <p>You don't have permission to access this resource.</p>
  </body>
</html>)"},
    {404, "404 Not Found",
     R"(<!DOCTYPE html>
<html>
  <head>
    <title>404 Not Found</title>
  </head>
  <body>
    <h1>Not Found</h1>
    <p>The requested URL was not found on this server.</p>
  </body>
</html>)"},
    {500, "500 Internal Server Error",
     R"(<!DOCTYPE html>
<html>
  <head>
    <title>500 Internal Server Error</title>
  </head>
  <body>
    <h1>Internal Server Error</h1>
    <p>The server encountered an internal error and was unable to complete your request.</p>
  </body>
</html>)"},
    {501, "501 Not Implemented",
     R"(<!DOCTYPE html>
<html>
  <head>
    <title>501 Not Implemented</title>
  </head>
  <body>
    <h1>Not Implemented</h1>
    <p>The server does not support the functionality required to fulfill the request.</p>
  </body>
</html>)"},
});

template <int Code> struct simple_response {
  int code() const { return Code; }
  std::string_view header() const {
    auto it = std::ranges::find(http_responses, code(), &http_response::code);
    assert(it != http_responses.end());
    return it->header;
  }
  std::string_view content() const {
    auto it = std::ranges::find(http_responses, code(), &http_response::code);
    assert(it != http_responses.end());
    return it->message;
  }
  std::string_view mime_type() const {
    auto it = std::ranges::find(mime_types, ".html", &mime_type::extension);
    assert(it != mime_types.end());
    return it->type;
  }
};
} // namespace detail

struct r200 : detail::simple_response<200> { // OK
  std::filesystem::path file_path;

  std::string content() const;
  std::string_view mime_type() const;
};
struct r301 : detail::simple_response<301> { // Moved Permanently
  std::string new_location;

  std::string content() const {
    std::string res{detail::simple_response<301>::content()};
    res.replace(res.find("{}"), 2, new_location);
    return res;
  }
};
struct r403 : detail::simple_response<403> {}; // Forbidden
struct r404 : detail::simple_response<404> {}; // Not Found
struct r500 : detail::simple_response<500> {}; // Internal Server Error
struct r501 : detail::simple_response<501> {}; // Not Implemented

struct request {
  std::variant<r200, r301, r403, r404, r500, r501> data = r501{};
  bool keep_alive{true};
};

std::string get_response(std::string_view version, const request &req);

} // namespace http