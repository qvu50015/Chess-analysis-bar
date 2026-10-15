#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace chess_eval::vision {
enum class Orientation { WhiteBottom, BlackBottom };
// FEN symbols, '.' for empty, '?' for unknown. Canonical order a8..h1.
struct Square {
  char piece = '?';
  double confidence = 0;
};
struct Board {
  std::array<Square, 64> squares;
};
int canonicalIndex(int row, int column, Orientation orientation);
std::string squareName(int index);
std::vector<std::string> validate(const Board &board);
std::optional<std::string> placement(const Board &board);
Board parsePlacement(const std::string &text);
struct RecognitionResult {
  Board board;
  std::vector<std::string> errors;
  std::optional<std::string> fenPlacement;
  double milliseconds = 0;
};
} // namespace chess_eval::vision
