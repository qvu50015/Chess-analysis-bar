#include "chess_eval/uci.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace chess_eval {
namespace {
bool integer(const std::string &text, int &out) {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), out);
  return error == std::errc{} && end == text.data() + text.size() &&
         out != std::numeric_limits<int>::min();
}
} // namespace
std::optional<InfoScore> parse_info(std::string_view line) {
  std::istringstream input{std::string(line)};
  std::string token;
  if (!(input >> token) || token != "info")
    return std::nullopt;
  std::optional<Score> score;
  int depth = -1;
  int multipv = 1;
  while (input >> token) {
    // These fields consume the rest of the line. Never interpret their
    // contents.
    if (token == "string" || token == "pv")
      break;
    if (token == "depth" || token == "multipv") {
      const bool is_depth = token == "depth";
      int value;
      if (!(input >> token) || !integer(token, value) || value < 0)
        return std::nullopt;
      if (is_depth)
        depth = value;
      else
        multipv = value;
    } else if (token == "score") {
      if (score)
        return std::nullopt;
      std::string kind, value;
      int parsed;
      if (!(input >> kind >> value) || !integer(value, parsed))
        return std::nullopt;
      if (kind != "cp" && kind != "mate")
        return std::nullopt;
      score = Score{kind == "cp" ? ScoreKind::centipawns : ScoreKind::mate,
                    parsed, Bound::exact, kind == "mate" && parsed == 0};
    } else if (token == "lowerbound" || token == "upperbound") {
      if (!score || score->bound != Bound::exact)
        return std::nullopt;
      score->bound = token == "lowerbound" ? Bound::lower : Bound::upper;
    }
  }
  if (!score || depth < 0 || multipv != 1)
    return std::nullopt;
  return InfoScore{*score, depth};
}
Score white_relative(Score score, Side turn) {
  if (turn == Side::black) {
    if (score.value == std::numeric_limits<int>::min())
      throw std::overflow_error("Score cannot be negated");
    score.value = -score.value;
    if (score.kind == ScoreKind::mate && score.value == 0)
      score.negative_zero = !score.negative_zero;
    if (score.bound == Bound::lower)
      score.bound = Bound::upper;
    else if (score.bound == Bound::upper)
      score.bound = Bound::lower;
  }
  return score;
}
double white_share(Score score) {
  if (score.kind == ScoreKind::mate) {
    if (score.value == 0)
      return score.negative_zero ? 0.0 : 1.0;
    return score.value > 0 ? 1.0 : 0.0;
  }
  return 1.0 / (1.0 + std::exp(-static_cast<double>(score.value) / 400.0));
}
std::string format_score(Score score) {
  if (score.bound != Bound::exact)
    throw std::invalid_argument(
        "Cannot display a bound as an exact evaluation");
  return std::string(score.kind == ScoreKind::centipawns ? "cp " : "mate ") +
         (score.kind == ScoreKind::mate && score.value == 0 &&
                  score.negative_zero
              ? "-0"
              : std::to_string(score.value));
}
std::vector<std::string> LineBuffer::append(std::string_view chunk) {
  std::vector<std::string> lines;
  for (char c : chunk) {
    if (c == '\n') {
      if (!pending_.empty() && pending_.back() == '\r')
        pending_.pop_back();
      lines.push_back(std::move(pending_));
      pending_.clear();
    } else {
      if (pending_.size() >= 65536)
        throw std::runtime_error("Engine output line exceeds 64 KiB");
      pending_ += c;
    }
  }
  return lines;
}
} // namespace chess_eval
