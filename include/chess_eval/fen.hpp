#pragma once
#include "chess_eval/uci.hpp"
#include <string>
#include <string_view>
namespace chess_eval {
struct Position {
  std::string fen;
  Side turn;
};
Position parse_fen(std::string_view fen);
} // namespace chess_eval
