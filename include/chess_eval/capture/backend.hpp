#pragma once
#include "chess_eval/capture/types.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
namespace chess_eval::capture {
struct Event {
  uint64_t session;
  enum Kind { Started, Ended } kind;
  QString error;
};
// Bounded mailbox: no per-frame queued GUI invocations or unbounded frame
// history.
class Mailbox {
public:
  void publish(Frame frame);
  void event(Event event);
  void displays(std::vector<Display> displays, QString error = {});
  struct Update {
    std::optional<Frame> frame;
    std::vector<Event> events;
    std::optional<std::vector<Display>> displays;
    QString displayError;
  };
  Update take();

private:
  std::mutex mutex_;
  Update update_;
};
class Backend {
public:
  virtual ~Backend() = default;
  virtual void refreshDisplays() = 0;
  virtual void start(Region region, uint64_t session) = 0;
  virtual void stop() = 0;
};
using BackendFactory =
    std::function<std::unique_ptr<Backend>(std::shared_ptr<Mailbox>)>;
std::unique_ptr<Backend> makeMacBackend(std::shared_ptr<Mailbox> mailbox);
} // namespace chess_eval::capture
