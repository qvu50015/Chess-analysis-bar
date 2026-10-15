#include "chess_eval/capture/backend.hpp"
namespace chess_eval::capture {
void Mailbox::publish(Frame frame) {
  std::lock_guard lock(mutex_);
  update_.frame = std::move(frame);
}
void Mailbox::event(Event event) {
  std::lock_guard lock(mutex_);
  // Only one session can run; protect against a faulty backend flooding events.
  if (update_.events.size() >= 8)
    update_.events.erase(update_.events.begin());
  update_.events.push_back(std::move(event));
}
void Mailbox::displays(std::vector<Display> displays, QString error) {
  std::lock_guard lock(mutex_);
  update_.displays = std::move(displays);
  update_.displayError = std::move(error);
}
Mailbox::Update Mailbox::take() {
  std::lock_guard lock(mutex_);
  auto result = std::move(update_);
  update_ = {};
  return result;
}
} // namespace chess_eval::capture
