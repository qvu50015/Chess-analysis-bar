#pragma once
#include "chess_eval/cancellation.hpp"
#include "chess_eval/uci.hpp"
#include <chrono>
#include <deque>
#include <string>

namespace chess_eval {
// Single-owner blocking transport. Call from a worker when integrating a UI.
class Process {
public:
  using Clock = std::chrono::steady_clock;
  explicit Process(const std::string &executable);
  ~Process();
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;
  void send_line(const std::string &line, Clock::time_point deadline,
                 const CancelCheck &cancel = {});
  std::string read_line(Clock::time_point deadline,
                        const CancelCheck &cancel = {});
  void shutdown() noexcept;

private:
  int pid_ = -1;
  int socket_ = -1;
  LineBuffer buffer_;
  std::deque<std::string> lines_;
};
} // namespace chess_eval
