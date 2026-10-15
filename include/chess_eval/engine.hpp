#pragma once
#include "chess_eval/cancellation.hpp"
#include "chess_eval/fen.hpp"
#include <chrono>
#include <string>
namespace chess_eval {
struct EngineOptions {
  std::string executable = "stockfish";
  int depth = 15;
  std::chrono::milliseconds timeout{30000};
};
// One synchronous, isolated search per call; failures discard all engine state.
Score analyze(const Position &position, const EngineOptions &options,
              const CancelCheck &cancel = {});
} // namespace chess_eval
