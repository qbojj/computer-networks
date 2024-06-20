#include "httpInfo.hpp"

#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <string>

using namespace http;

std::string r200::content() const {
  // get content of the file
  std::ifstream file(file_path, std::ios::binary);
  file.exceptions(std::ios::badbit | std::ios::failbit);

  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

std::string_view r200::mime_type() const {
  // get mime_type type of the file
  if (auto it = std::ranges::find(http::mime_types, file_path.extension(),
                                  &http::mime_type::extension);
      it != http::mime_types.end())
    return it->type;

  return "application/octet-stream";
}

std::string http::get_response(std::string_view version, const request &resp) {
  std::string response;
  response.reserve(1024);

  auto append = [&](std::string_view header, std::string_view value) {
    response.append(header);
    response.append(": ");
    response.append(value);
    response.append("\r\n");
  };

  std::visit(
      [&](auto &data) {
        response.append(version);
        response.append(" ");
        response.append(data.header());
        response.append("\r\n");

        append("Content-Type", data.mime_type());
        append("Content-Length", std::to_string(data.content().size()));
        
        if (!resp.keep_alive)
          append("Connection", "close");
      },
      resp.data);

  // return-code specific headers
  std::visit(
      utils::overload{
          [&](const r301 &data) {
            append("Location", data.new_location);
          }, [&](auto) {}
      },
      resp.data);

  response.append("\r\n");

  // content
  std::visit([&](auto &data) { response.append(data.content()); }, resp.data);

  return response;
}