#include "chess_eval/capture/manager.hpp"
namespace chess_eval::capture {
Manager::Manager(BackendFactory factory, QObject *parent)
    : QObject(parent), mailbox_(std::make_shared<Mailbox>()),
      backend_(factory(mailbox_)) {
  timer_.setInterval(80);
  connect(&timer_, &QTimer::timeout, this, &Manager::poll);
  timer_.start();
}
Manager::~Manager() { backend_->stop(); }
void Manager::refreshDisplays() { backend_->refreshDisplays(); }
void Manager::start(const Region &region) {
  if (busy())
    return;
  try {
    const auto id = session_.start(region);
    emit cleared();
    emit statusChanged("Starting capture…");
    backend_->start(region, id);
  } catch (const std::exception &e) {
    emit statusChanged("Error: " + QString::fromUtf8(e.what()));
  }
}
void Manager::stop() {
  if (!session_.stop())
    return;
  emit cleared();
  emit statusChanged("Stopping capture…");
  backend_->stop();
}
void Manager::poll() {
  auto update = mailbox_->take();
  if (update.displays) {
    displays_ = std::move(*update.displays);
    emit displaysChanged();
    if (!update.displayError.isEmpty())
      emit statusChanged("Error: " + update.displayError);
  }
  for (const auto &event : update.events) {
    if (event.kind == Event::Started && session_.started(event.session))
      emit statusChanged("Capturing · 12 FPS target");
    if (event.kind == Event::Ended &&
        session_.ended(event.session, !event.error.isEmpty())) {
      emit cleared();
      emit statusChanged(event.error.isEmpty() ? "Idle"
                                               : "Error: " + event.error);
      emit idle();
    }
  }
  if (update.frame && session_.accepts(*update.frame))
    emit frameReady(*update.frame);
}
} // namespace chess_eval::capture
