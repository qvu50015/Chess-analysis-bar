#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chess_eval::chess {
struct Move {
  int from;
  int to;
  char promotion = 0; // Lowercase q/r/b/n, independent of moving side.
  bool operator==(const Move &) const = default;
};
class Game {
public:
  static constexpr const char *InitialFen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  explicit Game(const std::string &fen = InitialFen);
  std::string fen() const;
  std::string placement() const;
  std::vector<Move> legalMoves() const;
  Game play(Move move) const;
  std::vector<Game> successors() const;
  bool inCheck(bool white) const;
  const std::array<char, 64> &squares() const { return board_; }
  bool whiteToMove() const { return white_; }

private:
  Game apply(Move move) const;
  bool attacked(int square, bool byWhite) const;
  std::array<char, 64> board_{};
  bool white_ = true;
  std::string rights_;
  int ep_ = -1;
  uint64_t halfmove_ = 0, fullmove_ = 1;
};
} // namespace chess_eval::chess
