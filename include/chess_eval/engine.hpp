#pragma once
#include "chess_eval/cancellation.hpp"
#include "chess_eval/fen.hpp"
#include <chrono>
#include <functional>
#include <string>
namespace chess_eval {
struct EngineOptions {
  std::string executable = "stockfish";
  int depth = 15;
  std::chrono::milliseconds timeout{30000};
};
// One synchronous, isolated search per call; failures discard all engine state.
// Optional progress receives white-relative exact scores and depth on the
// caller's thread. Intermediate searches can change as the engine deepens.
Score analyze(const Position &position, const EngineOptions &options,
              const CancelCheck &cancel = {},
              const std::function<void(Score, int)> &progress = {});
} // namespace chess_eval
