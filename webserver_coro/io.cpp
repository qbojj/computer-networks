#include "io.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include <random>

using namespace io;

input_data io::parse_input(int argc, char *argv[]) {
  input_data res;

  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <port> <directory> [OPTIONS]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --debug: enable debug mode\n";
    std::cerr << "  --quotes [ms]: enable inspirational quotes\n";
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

  res.quote_interval = std::chrono::seconds(30);

  // rest of args are optional
  for (int i = 3; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--debug") {
      res.debug_mode = true;
    } else if (arg == "--quotes") {
      res.inspirational_quotes = true;
      if (i + 1 < argc) {
        std::string_view next_arg = argv[i + 1];
        if (next_arg.starts_with("--")) {
          // no interval specified
          continue;
        }

        res.quote_interval = std::chrono::milliseconds(std::stoi(argv[++i]));
      }
    } else {
      std::cerr << "Unknown option: " << argv[i] << '\n';
      throw std::invalid_argument("Unknown option");
    }
  }

  return res;
}

coro::eager_task<>
io::send_all(coro::io_engine &engine, const utils::handle &sock, std::span<const std::byte> data) {
  while (!data.empty()) {
    co_await engine.poll(sock, POLLOUT);

    ssize_t bytes_sent = send(sock, data.data(), data.size(), 0);
    if (bytes_sent < 0) {
      if (errno == EINTR)
        continue;
      
      utils::throw_sys_error("send");
    }
    data = data.subspan(bytes_sent);
  }
}

coro::eager_task<>
io::send_all(coro::io_engine &engine, const utils::handle &sock, std::string_view data) {
  return send_all(engine, sock, std::as_bytes(std::span(data.data(), data.size())));
}

coro::task io::quote_generator(coro::io_engine &engine, std::chrono::milliseconds interval) {
  auto quotes = std::array{
    "Programming is not about typing code, it's about thinking in algorithms.",
    "Template metaprogramming is like a puzzle game for C++ developers.",
    "RAII: Resource Acquisition Is Initialization - the secret weapon of C++.",
    "Smart pointers are the superheroes of memory management in C++.",
    "STL: Standard Template Library - the treasure trove of C++ algorithms and data structures.",
    "Concurrency in C++: embrace the power of threads and async tasks.",
    "Modern C++: where simplicity meets performance.",
    "Boost your productivity with C++17 and its powerful features.",
    "Debugging C++ code is like solving a mystery - one step at a time.",
    "Code reviews: the key to writing clean and maintainable C++ code.",
    "C++20: the future of C++ is here.",
    "C++ is a powerful and versatile programming language.",
    "Object-oriented programming allows for modular and reusable code.",
    "Memory management is crucial in C++ to avoid memory leaks and crashes.",
    "C++ offers strong type checking for safer and more reliable code.",
    "Exception handling in C++ helps to handle and recover from errors.",
    "C++ templates enable generic programming and code reuse.",
    "The C++ standard library provides a rich set of containers and algorithms.",
    "C++ supports multi-threading for concurrent and parallel programming.",
    "Unit testing is essential for ensuring the correctness of C++ code.",
    "Continuous integration and deployment improve the development process.",
    "C++ offers performance optimizations for efficient code execution.",
    "Code documentation is important for understanding and maintaining code.",
    "Version control systems like Git help manage code changes and collaboration.",
    "Software design patterns provide proven solutions to common programming problems.",
    "Refactoring improves code quality and maintainability over time.",
    "Code profiling and optimization can enhance the performance of C++ applications.",
    "C++ offers interoperability with other programming languages through language bindings.",
    "Static analysis tools help identify potential bugs and code quality issues.",
    "C++11 introduced many new features and improvements to the language.",
    "C++14 and C++17 further enhanced the language with additional features and libraries.",
    "C++20 introduced modules, concepts, and other language enhancements.",
    "Continuous learning and staying up-to-date with C++ developments is important for every developer.",
  };
  
  std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, quotes.size() - 1);

  while (true) {
    co_await engine.wait_for(interval);
    std::cout << "Quote of the day: \"" << quotes[dist(gen)] << "\"\n";
  }
}