#include "chess_eval/vision/board.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace chess_eval::vision {
int canonicalIndex(int row, int column, Orientation orientation) {
  if (row < 0 || row > 7 || column < 0 || column > 7)
    throw std::invalid_argument("Square outside 8x8 board");
  return orientation == Orientation::WhiteBottom ? row * 8 + column
                                                 : 63 - row * 8 - column;
}
std::string squareName(int index) {
  if (index < 0 || index >= 64)
    throw std::invalid_argument("Invalid square index");
  return std::string{static_cast<char>('a' + index % 8),
                     static_cast<char>('8' - index / 8)};
}
std::vector<std::string> validate(const Board &board) {
  std::vector<std::string> errors;
  int kings[2]{}, pawns[2]{}, counts[2]{};
  int material[2][4]{};
  bool unknown = false;
  for (int i = 0; i < 64; ++i) {
    const auto &s = board.squares[i];
    if (!std::isfinite(s.confidence) || s.confidence < 0 || s.confidence > 1 ||
        s.piece == '?' ||
        std::string(".PNBRQKpnbrqk").find(s.piece) == std::string::npos) {
      unknown = true;
      continue;
    }
    if (s.piece == '.')
      continue;
    const int color = s.piece >= 'a' ? 1 : 0;
    ++counts[color];
    const auto type = std::string("NBRQnbrq").find(s.piece);
    if (type != std::string::npos)
      ++material[color][type % 4];
    if (s.piece == 'K' || s.piece == 'k')
      ++kings[color];
    if (s.piece == 'P' || s.piece == 'p') {
      ++pawns[color];
      if (i < 8 || i >= 56)
        errors.push_back("Pawn on promotion rank: " + squareName(i));
    }
  }
  if (unknown)
    errors.push_back("Recognition incomplete: unknown or invalid squares");
  for (int color = 0; color < 2; ++color) {
    const std::string name = color ? "Black" : "White";
    if (kings[color] != 1)
      errors.push_back(name + " must have exactly one king");
    const int promotions = std::max(0, material[color][0] - 2) +
                           std::max(0, material[color][1] - 2) +
                           std::max(0, material[color][2] - 2) +
                           std::max(0, material[color][3] - 1);
    if (promotions > 8 - pawns[color])
      errors.push_back(
          name +
          " has excess pieces without enough missing pawns for promotion");
    if (pawns[color] > 8 || counts[color] > 16)
      errors.push_back(name + " has impossible piece counts");
  }
  return errors;
}
std::optional<std::string> placement(const Board &board) {
  if (!validate(board).empty())
    return std::nullopt;
  std::string text;
  for (int row = 0; row < 8; ++row) {
    int empty = 0;
    for (int col = 0; col < 8; ++col) {
      const auto piece = board.squares[row * 8 + col].piece;
      if (piece == '.')
        ++empty;
      else {
        if (empty) {
          text += static_cast<char>('0' + empty);
          empty = 0;
        }
        text += piece;
      }
    }
    if (empty)
      text += static_cast<char>('0' + empty);
    if (row != 7)
      text += '/';
  }
  return text;
}
Board parsePlacement(const std::string &text) {
  Board board;
  int row = 0, col = 0;
  for (char c : text) {
    if (c == '/') {
      if (col != 8 || row >= 7)
        throw std::invalid_argument("Invalid placement rows");
      ++row;
      col = 0;
      continue;
    }
    const int count = c >= '1' && c <= '8' ? c - '0' : 1;
    if (!(c >= '1' && c <= '8') &&
        std::string("PNBRQKpnbrqk").find(c) == std::string::npos)
      throw std::invalid_argument("Invalid placement symbol");
    if (col + count > 8)
      throw std::invalid_argument("Placement row overflow");
    for (int n = 0; n < count; ++n)
      board.squares[row * 8 + col++] = {c >= '1' && c <= '8' ? '.' : c, 1};
  }
  if (row != 7 || col != 8)
    throw std::invalid_argument("Placement must contain 64 squares");
  return board;
}
} // namespace chess_eval::vision
