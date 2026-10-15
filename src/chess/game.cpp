#include "chess_eval/chess/game.hpp"
#include "chess_eval/fen.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace chess_eval::chess {
namespace {
bool white(char p) { return p >= 'A' && p <= 'Z'; }
char kind(char p) { return static_cast<char>(std::tolower(p)); }
bool inside(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }
std::string name(int i) { return {char('a' + i % 8), char('8' - i / 8)}; }
} // namespace
Game::Game(const std::string &fen) {
  const auto valid = parse_fen(fen);
  std::istringstream input(valid.fen);
  std::string placement, turn, ep;
  input >> placement >> turn >> rights_ >> ep >> halfmove_ >> fullmove_;
  board_.fill('.');
  int i = 0;
  for (char p : placement) {
    if (p >= '1' && p <= '8')
      i += p - '0';
    else if (p != '/')
      board_[i++] = p;
  }
  white_ = turn == "w";
  if (rights_ == "-")
    rights_.clear();
  if (ep != "-")
    ep_ = ('8' - ep[1]) * 8 + ep[0] - 'a';
  if (inCheck(!white_))
    throw std::invalid_argument(
        "Invalid starting FEN: the side that just moved is in check");
}
std::string Game::placement() const {
  std::string result;
  for (int r = 0; r < 8; ++r) {
    int empty = 0;
    for (int c = 0; c < 8; ++c) {
      char p = board_[r * 8 + c];
      if (p == '.')
        ++empty;
      else {
        if (empty)
          result += char('0' + empty);
        empty = 0;
        result += p;
      }
    }
    if (empty)
      result += char('0' + empty);
    if (r != 7)
      result += '/';
  }
  return result;
}
std::string Game::fen() const {
  std::string rights;
  for (char r : std::string("KQkq"))
    if (rights_.find(r) != std::string::npos)
      rights += r;
  return placement() + (white_ ? " w " : " b ") +
         (rights.empty() ? "-" : rights) + " " + (ep_ < 0 ? "-" : name(ep_)) +
         " " + std::to_string(halfmove_) + " " + std::to_string(fullmove_);
}
bool Game::attacked(int square, bool byWhite) const {
  const int tr = square / 8, tc = square % 8;
  for (int from = 0; from < 64; ++from) {
    char p = board_[from];
    if (p == '.' || white(p) != byWhite)
      continue;
    int r = from / 8, c = from % 8, dr = tr - r, dc = tc - c;
    char k = kind(p);
    if (k == 'p' && dr == (byWhite ? -1 : 1) && std::abs(dc) == 1)
      return true;
    if (k == 'n' && std::abs(dr) * std::abs(dc) == 2)
      return true;
    if (k == 'k' && std::max(std::abs(dr), std::abs(dc)) == 1)
      return true;
    bool diagonal = dr != 0 && std::abs(dr) == std::abs(dc);
    bool straight = (dr == 0) != (dc == 0);
    if (!((diagonal && (k == 'b' || k == 'q')) ||
          (straight && (k == 'r' || k == 'q'))))
      continue;
    int sr = (dr > 0) - (dr < 0), sc = (dc > 0) - (dc < 0);
    r += sr;
    c += sc;
    while (r != tr || c != tc) {
      if (board_[r * 8 + c] != '.')
        break;
      r += sr;
      c += sc;
    }
    if (r == tr && c == tc)
      return true;
  }
  return false;
}
bool Game::inCheck(bool side) const {
  auto king = std::find(board_.begin(), board_.end(), side ? 'K' : 'k');
  return king == board_.end() || attacked(int(king - board_.begin()), !side);
}
Game Game::apply(Move m) const {
  Game next = *this;
  char p = board_[m.from], captured = board_[m.to];
  next.board_[m.from] = '.';
  next.board_[m.to] =
      m.promotion ? (white_ ? char(std::toupper(m.promotion)) : m.promotion)
                  : p;
  if (kind(p) == 'p' && m.to == ep_ && captured == '.' &&
      m.from % 8 != m.to % 8)
    next.board_[m.to + (white_ ? 8 : -8)] = '.';
  if (kind(p) == 'k' && std::abs(m.to - m.from) == 2) {
    int rook = m.to > m.from ? m.from + 3 : m.from - 4;
    next.board_[(m.from + m.to) / 2] = board_[rook];
    next.board_[rook] = '.';
  }
  auto remove = [&](char r) { std::erase(next.rights_, r); };
  if (p == 'K') {
    remove('K');
    remove('Q');
  }
  if (p == 'k') {
    remove('k');
    remove('q');
  }
  for (auto [square, right] : std::array<std::pair<int, char>, 4>{
           {{63, 'K'}, {56, 'Q'}, {7, 'k'}, {0, 'q'}}})
    if (m.from == square || m.to == square)
      remove(right);
  next.ep_ = kind(p) == 'p' && std::abs(m.to - m.from) == 16
                 ? (m.from + m.to) / 2
                 : -1;
  next.halfmove_ = kind(p) == 'p' || captured != '.' ? 0 : halfmove_ + 1;
  next.fullmove_ += !white_;
  next.white_ = !white_;
  return next;
}
std::vector<Move> Game::legalMoves() const {
  std::vector<Move> moves;
  auto add = [&](int from, int to) {
    if (board_[to] != '.' &&
        (white(board_[to]) == white_ || kind(board_[to]) == 'k'))
      return;
    if (kind(board_[from]) == 'p' && (to / 8 == 0 || to / 8 == 7)) {
      for (char p : std::string("qrbn"))
        moves.push_back({from, to, p});
    } else
      moves.push_back({from, to});
  };
  for (int from = 0; from < 64; ++from) {
    char p = board_[from];
    if (p == '.' || white(p) != white_)
      continue;
    int r = from / 8, c = from % 8;
    char k = kind(p);
    if (k == 'p') {
      int step = white_ ? -1 : 1, nr = r + step;
      if (!inside(nr, c))
        continue;
      if (board_[nr * 8 + c] == '.') {
        add(from, nr * 8 + c);
        if (r == (white_ ? 6 : 1) && board_[(r + 2 * step) * 8 + c] == '.')
          add(from, (r + 2 * step) * 8 + c);
      }
      for (int dc : {-1, 1})
        if (inside(nr, c + dc)) {
          int to = nr * 8 + c + dc;
          if (board_[to] != '.' ||
              (to == ep_ && board_[to - step * 8] == (white_ ? 'p' : 'P')))
            add(from, to);
        }
    } else {
      for (int dr = -2; dr <= 2; ++dr)
        for (int dc = -2; dc <= 2; ++dc) {
          if (!dr && !dc)
            continue;
          bool knight = std::abs(dr) * std::abs(dc) == 2;
          bool unit = std::max(std::abs(dr), std::abs(dc)) == 1;
          if (k == 'n' ? !knight : !unit)
            continue;
          if (k == 'b' && (!dr || !dc))
            continue;
          if (k == 'r' && dr && dc)
            continue;
          int nr = r + dr, nc = c + dc;
          while (inside(nr, nc)) {
            int to = nr * 8 + nc;
            add(from, to);
            if (board_[to] != '.' || k == 'k' || k == 'n')
              break;
            nr += dr;
            nc += dc;
          }
        }
      if (k == 'k' && from == (white_ ? 60 : 4) && !inCheck(white_)) {
        for (int direction : {-1, 1}) {
          char right = white_ ? (direction == 1 ? 'K' : 'Q')
                              : (direction == 1 ? 'k' : 'q');
          int rook = from + (direction == 1 ? 3 : -4);
          if (rights_.find(right) == std::string::npos ||
              board_[rook] != (white_ ? 'R' : 'r'))
            continue;
          bool clear = true;
          for (int s = from + direction; s != rook; s += direction)
            if (board_[s] != '.')
              clear = false;
          if (clear && !apply({from, from + direction}).inCheck(white_) &&
              !apply({from, from + 2 * direction}).inCheck(white_))
            add(from, from + 2 * direction);
        }
      }
    }
  }
  std::erase_if(moves, [&](Move m) { return apply(m).inCheck(white_); });
  return moves;
}
std::vector<Game> Game::successors() const {
  std::vector<Game> result;
  for (auto move : legalMoves())
    result.push_back(apply(move));
  return result;
}
Game Game::play(Move move) const {
  const auto legal = legalMoves();
  if (std::find(legal.begin(), legal.end(), move) == legal.end())
    throw std::invalid_argument("Illegal chess move");
  return apply(move);
}
} // namespace chess_eval::chess
