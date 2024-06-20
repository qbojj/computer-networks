#include "io.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace io;

input_data io::parse_input(int argc, char *argv[]) {
  input_data res;

  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <port> <directory>\n";
    throw std::invalid_argument("Invalid number of arguments");
  }

  res.port = htons(std::stoi(argv[1]));
  res.directory = std::filesystem::path(argv[2]);

  if (!std::filesystem::exists(res.directory)) {
    std::cerr << "Directory " << res.directory << " does not exist\n";
    throw std::invalid_argument("Directory does not exist");
  }

  if (!std::filesystem::is_directory(res.directory)) {
    std::cerr << res.directory << " is not a directory\n";
    throw std::invalid_argument("Not a directory");
  }

  return res;
}