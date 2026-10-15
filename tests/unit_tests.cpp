#include "chess_eval/fen.hpp"
#include "chess_eval/uci.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace chess_eval;
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("Unit assertion failed");
}
template <typename F> void rejects(F function) {
  bool caught = false;
  try {
    function();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught);
}
int main() {
  try {
    auto cp =
        parse_info("info depth 12 seldepth 20 score cp -43 nodes 100 pv e2e4");
    check(cp && cp->depth == 12 &&
          cp->score == Score{ScoreKind::centipawns, -43});
    auto mate = parse_info("info depth 8 score mate -3");
    check(mate && mate->score == Score{ScoreKind::mate, -3});
    check(parse_info("info depth 9 score cp 25 lowerbound")->score.bound ==
          Bound::lower);
    check(parse_info("info depth 9 score mate 2 upperbound")->score.bound ==
          Bound::upper);
    for (const auto *bad :
         {"", "bestmove e2e4", "info depth 2", "info score cp 20",
          "info depth x score cp 3", "info depth 2 score cp",
          "info depth 2 score cp 12x", "info depth 2 score cp 999999999999999",
          "info depth 2 score cp -2147483648", "info depth 2 score xyz 1",
          "info depth 2 score cp 1 score cp 2",
          "info depth 2 lowerbound score cp 2",
          "info depth 2 score cp 1 lowerbound upperbound",
          "info string depth 2 score cp 10", "info depth 2 pv score cp 10",
          "info depth 2 multipv 2 score cp 15"})
      check(!parse_info(bad));
    check(parse_info("info depth 2 score cp 1 pv score mate 3")->score.value ==
          1);
    check(white_relative(cp->score, Side::white).value == -43);
    check(white_relative(cp->score, Side::black).value == 43);
    check(white_relative(mate->score, Side::black).value == 3);
    check(white_relative({ScoreKind::centipawns, 0}, Side::black).value == 0);
    check(white_relative({ScoreKind::centipawns, 20, Bound::lower}, Side::black)
              .bound == Bound::upper);
    check(white_relative({ScoreKind::centipawns, 20, Bound::upper}, Side::black)
              .bound == Bound::lower);
    check(format_score({ScoreKind::centipawns, -42}) == "cp -42");
    check(format_score({ScoreKind::mate, 3}) == "mate 3");
    rejects([] { format_score({ScoreKind::centipawns, 20, Bound::upper}); });
    check(white_share({ScoreKind::centipawns, 0}) == 0.5);
    auto moderate = white_share({ScoreKind::centipawns, 200});
    check(moderate > 0.5 && moderate < 1.0);
    check(std::abs(moderate + white_share({ScoreKind::centipawns, -200}) -
                   1.0) < 1e-12);
    check(white_share({ScoreKind::centipawns, 1000000}) == 1.0);
    check(white_share({ScoreKind::centipawns, -1000000}) == 0.0);
    check(white_share({ScoreKind::mate, 3}) == 1.0);
    check(white_share({ScoreKind::mate, -3}) == 0.0);
    auto zero = parse_info("info depth 0 score mate 0")->score;
    check(format_score(white_relative(zero, Side::white)) == "mate -0");
    check(format_score(white_relative(zero, Side::black)) == "mate 0");
    check(white_share(white_relative(zero, Side::white)) == 0.0);
    check(white_share(white_relative(zero, Side::black)) == 1.0);
    LineBuffer buffer;
    check(buffer.append("info depth 2 sco").empty());
    auto lines = buffer.append("re cp 12\r\nbestmove e2e4\npar");
    check(lines.size() == 2 && parse_info(lines[0])->score.value == 12);
    check(buffer.append("tial\n")[0] == "partial");
    rejects([] {
      LineBuffer b;
      b.append(std::string(65537, 'a'));
    });
    check(parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
              .turn == Side::white);
    check(parse_fen("4k3/8/8/8/8/8/8/4K3 b - - 0 1").turn == Side::black);
    check(parse_fen("4k3/8/8/3p4/8/8/8/4K3 w - d6 0 2").turn == Side::white);
    for (const auto *bad :
         {"", "8/8/8/8/8/8/8/8 w - - 0 1", "4k3/8/8/8/8/8/8/4K3 x - - 0 1",
          "4k3/8/8/8/8/8/8/4K3 w K - 0 1", "4k3/8/8/8/8/8/8/4K3 w - e6 0 1",
          "4k3/8/8/8/8/8/8/4K3 w - - 0 0", "4k3/8/8/8/8/8/8/4K3 w - - -1 1",
          "4k3/8/8/8/8/8/8/4K3 w - - 0 1\ngo depth 1",
          "8/8/8/8/8/8/4k3/4K3 w - - 0 1", "4k3/8/8/8/8/8/8/P3K3 w - - 0 1"})
      rejects([&] { parse_fen(bad); });
    std::cout
        << "All parser, conversion, mapping, framing, and FEN tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
