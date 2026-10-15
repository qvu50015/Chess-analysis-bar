#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess_eval {
enum class Side { white, black };
enum class ScoreKind { centipawns, mate };
enum class Bound { exact, lower, upper };
struct Score {
  ScoreKind kind;
  int value;
  Bound bound = Bound::exact;
  // Preserve the losing side for UCI mate 0 (already checkmated).
  bool negative_zero = false;
  bool operator==(const Score &) const = default;
};
struct InfoScore {
  Score score;
  int depth;
};
std::optional<InfoScore> parse_info(std::string_view line);
Score white_relative(Score score, Side turn);
double white_share(Score white_score);
std::string format_score(Score white_score);
class LineBuffer {
public:
  std::vector<std::string> append(std::string_view chunk);

private:
  std::string pending_;
};
} // namespace chess_eval
