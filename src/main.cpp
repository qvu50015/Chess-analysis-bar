#include "chess_eval/engine.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
  try {
    chess_eval::EngineOptions options;
    std::string fen;
    bool seen_fen = false, seen_engine = false, seen_depth = false,
         seen_timeout = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "Usage: chess-eval --fen '<FEN>' [--stockfish PATH] "
                     "[--depth 1..128] [--timeout-ms N]\n";
        return 0;
      }
      if (arg != "--fen" && arg != "--stockfish" && arg != "--depth" &&
          arg != "--timeout-ms")
        throw std::invalid_argument("Unknown option: " + arg);
      if (++i == argc)
        throw std::invalid_argument("Missing value for " + arg);
      std::string value = argv[i];
      bool &seen = arg == "--fen"         ? seen_fen
                   : arg == "--stockfish" ? seen_engine
                   : arg == "--depth"     ? seen_depth
                                          : seen_timeout;
      if (seen)
        throw std::invalid_argument("Duplicate option: " + arg);
      seen = true;
      if (arg == "--fen")
        fen = value;
      else if (arg == "--stockfish") {
        if (value.empty())
          throw std::invalid_argument("Stockfish path must not be empty");
        options.executable = value;
      } else {
        int parsed;
        auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || end != value.data() + value.size() ||
            parsed <= 0)
          throw std::invalid_argument("Expected positive integer for " + arg);
        if (arg == "--depth")
          options.depth = parsed;
        else
          options.timeout = std::chrono::milliseconds(parsed);
      }
    }
    if (!seen_fen)
      throw std::invalid_argument(
          "Required option: --fen '<FEN>' (see --help)");
    auto score = chess_eval::analyze(chess_eval::parse_fen(fen), options);
    std::cout << chess_eval::format_score(score) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "chess-eval: " << error.what() << '\n';
    return 1;
  }
}
