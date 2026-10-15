#pragma once
#include "chess_eval/cancellation.hpp"
#include "chess_eval/chess/game.hpp"
#include <optional>

namespace chess_eval::chess {
enum class TrackingState { Waiting, Current, Confirmed, Uncertain, Resync };
struct Observation {
  TrackingState state;
  std::optional<std::string> fen;
};
// No pixels or Qt: milliseconds are monotonic observation times supplied by the
// caller. Uncertainty never discards authoritative history; subsequent stable
// observations recover only through the current state or up to four legal
// plies. The complete recovered FEN must be unique, not just the visible
// placement.
class Tracker {
public:
  explicit Tracker(const std::string &fen) : game_(fen) {}
  Observation observe(const std::optional<std::string> &placement, int64_t ms,
                      const CancelCheck &cancel = {});
  const Game &game() const { return game_; }
  bool synchronized() const { return synchronized_; }
  void restartObservations();

private:
  Game game_;
  bool synchronized_ = false;
  std::optional<std::string> candidate_;
  std::optional<int64_t> uncertainSince_;
  int64_t candidateSince_ = 0, lastTime_ = -1;
  int samples_ = 0;
  std::optional<std::string> unmatched_;
};
} // namespace chess_eval::chess
