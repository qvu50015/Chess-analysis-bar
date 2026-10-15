#include "chess_eval/chess/tracker.hpp"
#include <functional>
namespace chess_eval::chess {
Observation Tracker::observe(const std::optional<std::string> &placement,
                             int64_t ms, const CancelCheck &cancel) {
  check_cancelled(cancel);
  if (ms <= lastTime_)
    return {TrackingState::Waiting, {}};
  lastTime_ = ms;
  if ((!placement || !synchronized_ || *placement != game_.placement()) &&
      !uncertainSince_)
    uncertainSince_ = ms;
  if (!placement) {
    candidate_.reset();
    samples_ = 0;
    return {TrackingState::Uncertain, {}};
  }
  if (candidate_ != placement) {
    candidate_ = placement;
    candidateSince_ = ms;
    samples_ = 1;
    return {TrackingState::Waiting, {}};
  }
  if (samples_ < 2)
    ++samples_;
  if (samples_ < 2 || ms - candidateSince_ < 300)
    return {TrackingState::Waiting, {}};
  if (*placement == game_.placement()) {
    uncertainSince_.reset();
    bool first = !synchronized_;
    synchronized_ = true;
    return {first ? TrackingState::Confirmed : TrackingState::Current,
            game_.fen()};
  }
  if (!synchronized_)
    return {TrackingState::Uncertain, {}};
  auto unmatched = [&]() -> Observation {
    return {uncertainSince_ && ms - *uncertainSince_ >= 3000
                ? TrackingState::Resync
                : TrackingState::Uncertain,
            {}};
  };
  if (unmatched_ == placement)
    return unmatched();
  std::optional<Game> match;
  bool ambiguous = false;
  auto consider = [&](const Game &next) {
    check_cancelled(cancel);
    if (next.placement() != *placement)
      return;
    if (match && match->fen() != next.fen())
      ambiguous = true;
    else
      match = next;
  };
  // Exhaust both depths before committing: never accept the first matching
  // pixels while another legal path could imply different hidden FEN fields.
  for (const auto &next : game_.successors()) {
    consider(next);
    for (const auto &reply : next.successors()) {
      consider(reply);
      if (ambiguous)
        break;
    }
    if (ambiguous)
      break;
  }
  // Only widen the search after ordinary move/reply recovery fails. Bound
  // work on the recognition worker; never commit a partially searched match.
  if (!match && !ambiguous) {
    std::array<char, 64> target;
    target.fill('.');
    size_t square = 0;
    for (char p : *placement) {
      if (p >= '1' && p <= '8')
        square += p - '0';
      else if (p != '/') {
        if (square >= target.size())
          return unmatched();
        target[square++] = p;
      }
    }
    if (square != target.size())
      return unmatched();
    size_t visited = 0;
    bool exhausted = false;
    auto reachable = [&](const Game &position, int remaining) {
      // Each missing destination needs a move by that color. Castling can
      // fill two destinations at once, so allow one extra for king + rook.
      for (bool white : {false, true}) {
        int missing = 0;
        bool king = false, rook = false;
        for (size_t i = 0; i < target.size(); ++i) {
          const char p = target[i];
          if (p == '.' || p == position.squares()[i] ||
              (p >= 'A' && p <= 'Z') != white)
            continue;
          ++missing;
          king |= p == (white ? 'K' : 'k');
          rook |= p == (white ? 'R' : 'r');
        }
        const int moves =
            remaining / 2 + (remaining % 2 && position.whiteToMove() == white);
        if (missing > moves + (moves > 0 && king && rook))
          return false;
      }
      return true;
    };
    std::function<void(const Game &, int)> search = [&](const Game &position,
                                                        int depth) {
      check_cancelled(cancel);
      if (++visited > 100000) {
        exhausted = true;
        return;
      }
      if (depth >= 3)
        consider(position);
      if (depth == 4 || ambiguous || !reachable(position, 4 - depth))
        return;
      for (const auto &next : position.successors()) {
        search(next, depth + 1);
        if (ambiguous || exhausted)
          return;
      }
    };
    search(game_, 0);
    if (exhausted)
      match.reset();
  }
  check_cancelled(cancel);
  if (!match || ambiguous) {
    unmatched_ = placement;
    return unmatched();
  }
  unmatched_.reset();
  game_ = std::move(*match);
  uncertainSince_.reset();
  return {TrackingState::Confirmed, game_.fen()};
}
void Tracker::restartObservations() {
  candidate_.reset();
  uncertainSince_.reset();
  candidateSince_ = 0;
  lastTime_ = -1;
  samples_ = 0;
  unmatched_.reset();
}
} // namespace chess_eval::chess
