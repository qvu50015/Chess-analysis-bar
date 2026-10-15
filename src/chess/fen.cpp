#include "chess_eval/fen.hpp"
#include <array>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace chess_eval {
Position parse_fen(std::string_view fen) {
  auto invalid = [] {
    throw std::invalid_argument("Invalid FEN: expected a valid board, side, "
                                "castling, en passant, and two move counters");
  };
  for (unsigned char c : fen)
    if (c < 32 || c == 127)
      invalid();
  std::istringstream input{std::string(fen)};
  std::array<std::string, 6> fields;
  for (auto &field : fields)
    if (!(input >> field))
      invalid();
  std::string extra;
  if (input >> extra)
    invalid();
  std::array<char, 64> board{};
  int rank = 0, file = 0, white_king = -1, black_king = -1;
  int white_count = 0, black_count = 0, white_pawns = 0, black_pawns = 0;
  for (char c : fields[0]) {
    if (c == '/') {
      if (file != 8 || rank == 7)
        invalid();
      ++rank;
      file = 0;
    } else if (c >= '1' && c <= '8') {
      file += c - '0';
      if (file > 8)
        invalid();
    } else {
      if (std::string_view("prnbqkPRNBQK").find(c) == std::string_view::npos ||
          file >= 8)
        invalid();
      const int square = rank * 8 + file++;
      board[square] = c;
      if (c >= 'A' && c <= 'Z')
        ++white_count;
      else
        ++black_count;
      if (c == 'P')
        ++white_pawns;
      if (c == 'p')
        ++black_pawns;
      if ((c == 'p' || c == 'P') && (rank == 0 || rank == 7))
        invalid();
      if (c == 'K') {
        if (white_king != -1)
          invalid();
        white_king = square;
      }
      if (c == 'k') {
        if (black_king != -1)
          invalid();
        black_king = square;
      }
    }
  }
  if (rank != 7 || file != 8 || white_king < 0 || black_king < 0 ||
      white_count > 16 || black_count > 16 || white_pawns > 8 ||
      black_pawns > 8)
    invalid();
  if (std::abs(white_king / 8 - black_king / 8) <= 1 &&
      std::abs(white_king % 8 - black_king % 8) <= 1)
    invalid();
  if (fields[1] != "w" && fields[1] != "b")
    invalid();
  if (fields[2] != "-") {
    std::string seen;
    for (char c : fields[2]) {
      if (std::string_view("KQkq").find(c) == std::string_view::npos ||
          seen.find(c) != std::string::npos)
        invalid();
      seen += c;
      const bool white = c == 'K' || c == 'Q';
      const int king = white ? 60 : 4;
      const int rook = c == 'K' ? 63 : c == 'Q' ? 56 : c == 'k' ? 7 : 0;
      if (board[king] != (white ? 'K' : 'k') ||
          board[rook] != (white ? 'R' : 'r'))
        invalid();
    }
  }
  if (fields[3] != "-") {
    const auto &ep = fields[3];
    if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' ||
        ep[1] != (fields[1] == "w" ? '6' : '3'))
      invalid();
    const int target = (8 - (ep[1] - '0')) * 8 + ep[0] - 'a';
    const int pawn = target + (fields[1] == "w" ? 8 : -8);
    const int origin = target + (fields[1] == "w" ? -8 : 8);
    if (board[target] || board[origin] ||
        board[pawn] != (fields[1] == "w" ? 'p' : 'P'))
      invalid();
  }
  for (int i : {4, 5}) {
    unsigned value;
    const auto &s = fields[i];
    const auto [end, error] =
        std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc{} || end != s.data() + s.size() ||
        (i == 5 && value == 0))
      invalid();
  }
  std::string canonical = fields[0];
  for (int i = 1; i < 6; ++i)
    canonical += " " + fields[i];
  return {canonical, fields[1] == "w" ? Side::white : Side::black};
}
} // namespace chess_eval
