#include "chess_eval/engine.hpp"
#include "chess_eval/process.hpp"
#include <sstream>
#include <stdexcept>

namespace chess_eval {
Score analyze(const Position &position, const EngineOptions &options) {
  if (options.depth < 1 || options.depth > 128 || options.timeout.count() <= 0)
    throw std::invalid_argument(
        "Depth must be 1..128 and timeout must be positive");
  const auto checked = parse_fen(position.fen);
  if (checked.turn != position.turn)
    throw std::invalid_argument("Position side does not match FEN");
  Process process(options.executable);
  auto wait_for = [&](const std::string &command, const std::string &response) {
    const auto deadline = Process::Clock::now() + options.timeout;
    process.send_line(command, deadline);
    while (process.read_line(deadline) != response) {
    }
  };
  wait_for("uci", "uciok");
  wait_for("isready", "readyok");
  const auto deadline = Process::Clock::now() + options.timeout;
  process.send_line("position fen " + checked.fen, deadline);
  process.send_line("go depth " + std::to_string(options.depth), deadline);
  std::optional<InfoScore> latest;
  for (;;) {
    const auto line = process.read_line(deadline);
    if (auto info = parse_info(line);
        info && info->score.bound == Bound::exact) {
      if (!latest || info->depth >= latest->depth)
        latest = info;
    }
    std::istringstream tokens(line);
    std::string first, completion;
    tokens >> first;
    if (first == "bestmove") {
      // UCI requires consuming the completion marker; no move is exposed or
      // analyzed.
      if (!(tokens >> completion))
        throw std::runtime_error("Malformed Stockfish completion marker");
      if (!latest)
        throw std::runtime_error(
            "Stockfish completed without an exact evaluation");
      return white_relative(latest->score, checked.turn);
    }
  }
}
} // namespace chess_eval
