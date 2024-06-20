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
  
  // format: adres IP , port , nazwa pliku i rozmiar
  if (argc != 5) {
    std::cerr << "Usage: " << argv[0] << " <ip> <port> <filename> <size>\n";
    throw std::invalid_argument("Invalid number of arguments");
  }

  res.ip = inet_addr(argv[1]);
  if (res.ip == INADDR_NONE) {
    std::cerr << "Invalid IP address\n";
    throw std::invalid_argument("Invalid IP address");
  }

  res.port = htons(std::stoi(argv[2]));
  
  res.file.open(argv[3], std::ios::binary);
  if (!res.file) {
    std::cerr << "Cannot open file\n";
    throw std::invalid_argument("Cannot open file");
  }

  res.size = std::stoull(argv[4]);

  return res;
}