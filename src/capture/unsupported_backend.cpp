#include "chess_eval/capture/backend.hpp"
namespace chess_eval::capture {
namespace {
class UnsupportedBackend : public Backend {
public:
  explicit UnsupportedBackend(std::shared_ptr<Mailbox> m)
      : mailbox_(std::move(m)) {}
  void refreshDisplays() override {
    mailbox_->displays({}, "Screen capture requires macOS 12.3 or later.");
  }
  void start(Region, uint64_t session) override {
    mailbox_->event({session, Event::Ended, "Screen capture requires macOS."});
  }
  void stop() override {}

private:
  std::shared_ptr<Mailbox> mailbox_;
};
} // namespace
std::unique_ptr<Backend> makeMacBackend(std::shared_ptr<Mailbox> mailbox) {
  return std::make_unique<UnsupportedBackend>(std::move(mailbox));
}
} // namespace chess_eval::capture
