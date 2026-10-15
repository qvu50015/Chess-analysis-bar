#include "chess_eval/engine.hpp"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
using namespace chess_eval;
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("Engine test assertion failed");
}
int main(int argc, char **argv) {
  try {
    check(argc == 2);
    EngineOptions options{argv[1], 4, std::chrono::milliseconds(500)};
    auto position = parse_fen("4k3/8/8/8/8/8/8/4K3 b - - 0 1");
    for (const auto *mode : {"normal", "mate", "bounds", "malformed", "eof",
                             "hang", "search_hang", "search_eof"}) {
      setenv("CHESS_EVAL_TEST_MODE", mode, 1);
      std::optional<Score> score;
      try {
        score = analyze(position, options);
      } catch (const std::exception &) {
      }
      const bool success =
          std::string(mode) == "normal" || std::string(mode) == "mate";
      check(score.has_value() == success);
      if (score) {
        check(score->value == (std::string(mode) == "mate" ? 2 : -31));
        check(score->kind == (std::string(mode) == "mate"
                                  ? ScoreKind::mate
                                  : ScoreKind::centipawns));
      }
      // Every path must reap its child, including forced termination.
      check(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
    }
    options.executable = "/nonexistent/chess-eval-stockfish";
    bool failed = false;
    try {
      analyze(position, options);
    } catch (const std::exception &) {
      failed = true;
    }
    check(failed);
    std::cout << "All engine lifecycle tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
