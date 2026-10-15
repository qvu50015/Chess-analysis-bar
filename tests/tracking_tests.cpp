#include "chess_eval/chess/tracker.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace chess_eval::chess;
void check(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}
Move move(const std::string &s) {
  return {('8' - s[1]) * 8 + s[0] - 'a', ('8' - s[3]) * 8 + s[2] - 'a',
          s.size() == 5 ? s[4] : char(0)};
}
Game play(Game g, const std::string &s) { return g.play(move(s)); }
bool legal(const Game &g, const std::string &s) {
  auto m = g.legalMoves();
  return std::find(m.begin(), m.end(), move(s)) != m.end();
}
uint64_t perft(const Game &g, int depth) {
  if (!depth)
    return 1;
  uint64_t n = 0;
  for (auto m : g.legalMoves())
    n += perft(g.play(m), depth - 1);
  return n;
}
int main() {
  try {
    Game g;
    check(perft(g, 1) == 20 && perft(g, 2) == 400 && perft(g, 3) == 8902,
          "Initial-position legal move tree");
    Game kiwipete(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    check(perft(kiwipete, 1) == 48 && perft(kiwipete, 2) == 2039,
          "Castling/check move tree");
    Game endgame("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    check(perft(endgame, 1) == 14 && perft(endgame, 2) == 191 &&
              perft(endgame, 3) == 2812,
          "En passant and rook endgame move tree");
    g = play(g, "e2e4");
    check(g.fen() ==
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
          "Double pawn FEN");
    g = play(g, "a7a6");
    g = play(g, "e4e5");
    g = play(g, "d7d5");
    g = play(g, "e5d6");
    check(g.fen() ==
              "rnbqkbnr/1pp1pppp/p2P4/8/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3",
          "En passant capture state");
    Game castle("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 4 12");
    check(play(castle, "e1g1").fen() == "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 5 12",
          "White castling");
    check(play(play(castle, "e1c1"), "e8g8").fen() ==
              "r4rk1/8/8/8/8/8/8/2KR3R w - - 6 13",
          "Both colors castle and counters");
    check(play(castle, "a1a8").fen().find(" b Kk ") != std::string::npos,
          "Rook move and capture remove rights");
    Game throughCheck("4kr2/8/8/8/8/8/8/4K2R w K - 0 1");
    check(!legal(throughCheck, "e1g1"), "Cannot castle through check");
    Game pin("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    check(!legal(pin, "e2f2"), "Pinned piece cannot expose king");
    Game epPin("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    check(!legal(epPin, "e5d6"), "En passant cannot expose king");
    Game promotion("7k/P7/8/8/8/8/8/4K3 w - - 0 1");
    for (char p : std::string("qrbn"))
      check(legal(promotion, std::string("a7a8") + p), "All promotion choices");
    check(play(promotion, "a7a8n").fen() == "N6k/8/8/8/8/8/8/4K3 b - - 0 1",
          "Underpromotion FEN");
    auto trackMove = [](const Game &before, const std::string &notation) {
      auto after = play(before, notation);
      Tracker t(before.fen());
      t.observe(before.placement(), 0);
      t.observe(before.placement(), 300);
      t.observe(after.placement(), 400);
      check(t.observe(after.placement(), 700).fen == after.fen(),
            "Tracker preserves complete FEN for special moves");
    };
    trackMove(castle, "e1c1");
    trackMove(play(castle, "e1c1"), "e8g8");
    trackMove(Game("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 7"), "e5d6");
    trackMove(Game("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 7"), "e4d3");
    trackMove(promotion, "a7a8n");
    trackMove(Game("4k3/8/8/8/8/8/p7/7K b - - 0 7"), "a2a1q");
    trackMove(castle, "a1a8");
    Game mate("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1");
    check(mate.legalMoves().empty() && mate.inCheck(false), "Checkmate");
    Game stalemate("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    check(stalemate.legalMoves().empty() && !stalemate.inCheck(false),
          "Stalemate");
    Tracker tracker(Game::InitialFen);
    auto start = Game().placement();
    auto e4 = play(Game(), "e2e4");
    check(tracker.observe(start, 0).state == TrackingState::Waiting,
          "Initial observation not confirmed");
    check(tracker.observe(start, 300).state == TrackingState::Confirmed,
          "Known start synchronized");
    check(tracker.observe({}, 400).state == TrackingState::Uncertain,
          "Animation freezes");
    check(tracker.observe(e4.placement(), 500).state == TrackingState::Waiting,
          "Move settles");
    check(tracker.observe(e4.placement(), 800).fen == e4.fen(),
          "Completed legal move confirmed");
    check(tracker.observe(e4.placement(), 900).state == TrackingState::Current,
          "Unchanged board");
    check(tracker.observe(start, 1000).state == TrackingState::Waiting,
          "Reset never guessed");
    check(tracker.observe(start, 4000).state == TrackingState::Resync,
          "Reset requires resync");
    tracker.observe(e4.placement(), 4500);
    check(tracker.observe(e4.placement(), 4800).state == TrackingState::Current,
          "Readable tracked position recovers after a long mismatch");
    Tracker missed(Game::InitialFen);
    missed.observe(start, 0);
    missed.observe(start, 300);
    auto two = play(e4, "e7e5");
    missed.observe(two.placement(), 400);
    check(missed.observe(two.placement(), 700).fen == two.fen(),
          "A fast move and reply recover the exact full FEN");
    check(missed.observe(two.placement(), 3400).state == TrackingState::Current,
          "Recovered position stays synchronized");
    auto recoverPair = [](const Game &before, const std::string &first,
                          const std::string &second) {
      auto after = play(play(before, first), second);
      Tracker t(before.fen());
      t.observe(before.placement(), 0);
      t.observe(before.placement(), 300);
      t.observe({}, 400);
      t.observe(after.placement(), 500);
      check(t.observe(after.placement(), 800).fen == after.fen(),
            "Two-ply recovery preserves special moves and hidden state");
    };
    recoverPair(castle, "e1c1", "e8g8");
    recoverPair(Game("4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 7"), "d7d5", "e5d6");
    recoverPair(promotion, "a7a8n", "h8g8");
    recoverPair(Game("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 4"), "e4d5", "e8d7");
    Tracker tooFar(Game::InitialFen);
    tooFar.observe(start, 0);
    tooFar.observe(start, 300);
    auto three = play(play(e4, "d7d5"), "e4d5");
    tooFar.observe(three.placement(), 400);
    check(tooFar.observe(three.placement(), 700).fen == three.fen(),
          "Three missed plies automatically recover the exact FEN");
    Tracker four(Game::InitialFen);
    four.observe(start, 0);
    four.observe(start, 300);
    auto fourMoves = play(three, "d8d5");
    four.observe(fourMoves.placement(), 400);
    check(four.observe(fourMoves.placement(), 700).fen == fourMoves.fen(),
          "Four missed plies automatically recover the exact FEN");
    Tracker ambiguous(Game::InitialFen);
    ambiguous.observe(start, 0);
    ambiguous.observe(start, 300);
    auto differentCounters = play(two, "g1f3");
    ambiguous.observe(differentCounters.placement(), 400);
    check(!ambiguous.observe(differentCounters.placement(), 700).fen &&
              ambiguous.game().fen() == Game::InitialFen,
          "Ambiguous hidden counters never overwrite history");
    Tracker cancelled(Game::InitialFen);
    cancelled.observe(start, 0);
    cancelled.observe(start, 300);
    cancelled.observe(two.placement(), 400);
    int checks = 0;
    bool interrupted = false;
    try {
      cancelled.observe(two.placement(), 700, [&] { return ++checks > 10; });
    } catch (const chess_eval::AnalysisCancelled &) {
      interrupted = true;
    }
    check(interrupted && cancelled.game().fen() == Game::InitialFen,
          "Cancellation during recovery never commits a partial match");
    check(cancelled.observe(two.placement(), 800).fen == two.fen(),
          "A cancelled recovery can be retried");
    Tracker wrong(Game::InitialFen);
    wrong.observe(e4.placement(), 0);
    check(wrong.observe(e4.placement(), 300).state == TrackingState::Uncertain,
          "Must observe known start first");
    Tracker recovery(Game::InitialFen);
    recovery.observe(start, 0);
    recovery.observe(start, 300);
    recovery.observe({}, 400);
    recovery.observe(start, 500);
    check(recovery.observe(start, 800).fen == Game::InitialFen,
          "Temporary artifacts recover without inventing a move");
    recovery.observe({}, 900);
    check(recovery.observe({}, 3900).state == TrackingState::Uncertain,
          "Persistent unreadable frames freeze without discarding history");
    recovery.observe(e4.placement(), 4000);
    check(recovery.observe(e4.placement(), 4300).fen == e4.fen(),
          "Legal move recovers automatically after more than three seconds");
    recovery.restartObservations();
    recovery.observe(e4.placement(), 0);
    check(recovery.observe(e4.placement(), 300).fen == e4.fen(),
          "Resume preserves turn, rights and counters");
    Tracker waiting(Game::InitialFen);
    waiting.observe({}, 0);
    waiting.observe({}, 5000);
    waiting.observe(start, 5100);
    check(waiting.observe(start, 5400).fen == Game::InitialFen,
          "Waiting for a new game has no three-second deadline");
    std::cout << "Legal moves, special moves, FEN and stable tracking passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
