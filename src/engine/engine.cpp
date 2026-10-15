#include "chess_eval/engine.hpp"
#include "chess_eval/process.hpp"
#include <sstream>
#include <stdexcept>

namespace chess_eval {
Score analyze(const Position &position, const EngineOptions &options,
              const CancelCheck &cancel,
              const std::function<void(Score, int)> &progress) {
  check_cancelled(cancel);
  if (options.depth < 1 || options.depth > 128 || options.timeout.count() <= 0)
    throw std::invalid_argument(
        "Depth must be 1..128 and timeout must be positive");
  const auto checked = parse_fen(position.fen);
  if (checked.turn != position.turn)
    throw std::invalid_argument("Position side does not match FEN");
  Process process(options.executable);
  try {
    auto wait_for = [&](const std::string &command,
                        const std::string &response) {
      const auto deadline = Process::Clock::now() + options.timeout;
      process.send_line(command, deadline, cancel);
      while (process.read_line(deadline, cancel) != response) {
      }
    };
    wait_for("uci", "uciok");
    wait_for("isready", "readyok");
    const auto deadline = Process::Clock::now() + options.timeout;
    process.send_line("position fen " + checked.fen, deadline, cancel);
    process.send_line("go depth " + std::to_string(options.depth), deadline,
                      cancel);
    std::optional<InfoScore> latest;
    for (;;) {
      const auto line = process.read_line(deadline, cancel);
      if (auto info = parse_info(line);
          info && info->score.bound == Bound::exact) {
        if (!latest || info->depth >= latest->depth) {
          latest = info;
          if (progress)
            progress(white_relative(info->score, checked.turn), info->depth);
        }
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
  } catch (const AnalysisCancelled &) {
    // Drain completion before reaping; a broken engine has a bounded grace
    // period.
    try {
      const auto stop_deadline =
          Process::Clock::now() + std::chrono::milliseconds(300);
      process.send_line("stop", stop_deadline);
      while (!process.read_line(stop_deadline).starts_with("bestmove ")) {
      }
    } catch (const std::exception &) {
    }
    throw;
  }
}
} // namespace chess_eval
