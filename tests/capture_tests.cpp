#include "chess_eval/capture/manager.hpp"
#include "chess_eval/ui/capture_panel.hpp"
#include "chess_eval/ui/region_selector.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace chess_eval::capture;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
void waitFor(const std::function<bool()> &predicate) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < 2000) {
    QEventLoop loop;
    QTimer::singleShot(10, &loop, &QEventLoop::quit);
    loop.exec();
  }
  check(predicate(), "Timed out");
}
class MockBackend : public Backend {
public:
  explicit MockBackend(std::shared_ptr<Mailbox> m) : mailbox(std::move(m)) {}
  void refreshDisplays() override {
    mailbox->displays({{7, "Mock Retina", {-1440, -200, 1440, 900}, 2}});
  }
  void start(Region, uint64_t id) override {
    session = id;
    ++starts;
  }
  void stop() override { ++stops; }
  std::shared_ptr<Mailbox> mailbox;
  uint64_t session = 0;
  int starts = 0, stops = 0;
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    Display display{7, "Retina", {-1440, -200, 1440, 900}, 2};
    Region region{display, {10, 20, 320, 200}};
    check(validRegion(region), "Valid rectangle");
    check(pixelRect(region) == QRect(20, 40, 640, 400),
          "Retina points to pixels");
    const auto global = regionFromGlobal(display, {-1430, -180, 320, 200});
    check(global && *global == region, "Negative multi-monitor origin");
    check(!regionFromGlobal(display, {-1500, -200, 100, 100}),
          "Cross-display selection rejected");
    for (auto rect :
         {QRectF(0, 0, 0, 10), QRectF(-1, 0, 20, 20), QRectF(1400, 0, 41, 20),
          QRectF(0, 899, 20, 2), QRectF(0, 0, -20, 20),
          QRectF(0, 0, std::numeric_limits<double>::quiet_NaN(), 20)}) {
      auto invalid = region;
      invalid.rect = rect;
      check(!validRegion(invalid), "Invalid bounds rejected");
    }
    auto fractional = region;
    fractional.display.scale = 1.5;
    fractional.rect = {0.5, 0.5, 10, 10};
    check(pixelRect(fractional) == QRect(0, 0, 16, 16),
          "Fractional scale encloses pixel bounds");
    auto one = region;
    one.display.scale = 1;
    check(pixelRect(one).size() == QSize(320, 200), "Non-Retina scale");
    one.display.scale = 0;
    check(!validRegion(one), "Invalid scale");
    check(aspectFit({640, 400}, {0, 0, 300, 300}) ==
              QRectF(0, 56.25, 300, 187.5),
          "Letterboxing");
    check(aspectFit({100, 200}, {0, 0, 400, 200}) == QRectF(150, 0, 100, 200),
          "Pillarboxing");
    check(aspectFit({}, {0, 0, 100, 100}).isEmpty(), "Empty aspect source");
    Frame frame{QImage(640, 400, QImage::Format_ARGB32), region, 1, 123};
    frame.pixels.fill(Qt::red);
    check(validFrame(frame), "Valid metadata");
    auto bad = frame;
    bad.timestampNs = -1;
    check(!validFrame(bad), "Invalid timestamp");
    bad = frame;
    bad.session = 0;
    check(!validFrame(bad), "Invalid session");
    bad = frame;
    bad.pixels = QImage(20, 20, QImage::Format_ARGB32);
    check(!validFrame(bad), "Mismatched dimensions");
    bad = frame;
    bad.pixels = frame.pixels.convertToFormat(QImage::Format_RGB888);
    check(!validFrame(bad), "Invalid pixel layout");
    Session session;
    check(session.state() == State::Idle && session.start(region) == 1,
          "Idle to starting");
    check(!session.accepts(frame) && !session.started(2),
          "No early/stale frames");
    check(session.started(1) && session.accepts(frame),
          "Starting to capturing");
    check(session.stop() && !session.accepts(frame) && !session.started(1),
          "Stop invalidates immediately");
    check(!session.ended(2, false) && session.ended(1, false),
          "Only active stop completes");
    check(session.start(region) == 2 && session.started(2) &&
              !session.accepts(frame),
          "Restart rejects prior session");
    check(session.ended(2, true) && session.state() == State::Error,
          "Capture error state");
    check(session.start(region) == 3 && session.stop() &&
              session.ended(3, false),
          "Stop during startup");
    Mailbox mailbox;
    std::thread producer([&] {
      for (int i = 0; i < 1000; ++i) {
        auto next = frame;
        next.timestampNs = i;
        mailbox.publish(std::move(next));
      }
    });
    producer.join();
    auto update = mailbox.take();
    check(update.frame && update.frame->timestampNs == 999 &&
              !mailbox.take().frame,
          "Only latest frame retained");
    MockBackend *mock = nullptr;
    Manager manager([&](auto inbox) {
      auto backend = std::make_unique<MockBackend>(inbox);
      mock = backend.get();
      return backend;
    });
    int frames = 0;
    QString status;
    QObject::connect(&manager, &Manager::frameReady, &app,
                     [&](const Frame &) { ++frames; });
    QObject::connect(&manager, &Manager::statusChanged, &app,
                     [&](const QString &text) { status = text; });
    manager.refreshDisplays();
    waitFor([&] { return !manager.displays().empty(); });
    manager.start(region);
    manager.start(region);
    check(mock->starts == 1, "Overlapping starts ignored");
    mock->mailbox->event({mock->session, Event::Started, {}});
    mock->mailbox->publish(frame);
    waitFor([&] { return frames == 1; });
    manager.stop();
    mock->mailbox->publish(frame);
    mock->mailbox->event({1, Event::Ended, {}});
    waitFor([&] { return !manager.busy(); });
    check(frames == 1, "Frame dropped after Stop");
    manager.start(region);
    mock->mailbox->event({2, Event::Started, {}});
    mock->mailbox->publish(frame);
    waitFor([&] { return manager.state() == State::Capturing; });
    check(frames == 1, "Old frame rejected after restart");
    mock->mailbox->event({1, Event::Ended, "Old error"});
    frame.session = 2;
    mock->mailbox->publish(frame);
    waitFor([&] { return frames == 2; });
    check(manager.state() == State::Capturing,
          "Old errors cannot stop new session");
    mock->mailbox->event({2, Event::Ended, "Permission denied"});
    waitFor([&] { return manager.state() == State::Error; });
    check(status.contains("Permission denied"),
          "Actionable backend errors surfaced");
    chess_eval::ui::CapturePreview preview;
    preview.resize(300, 300);
    preview.show();
    preview.setFrame(frame);
    app.processEvents();
    const auto image = preview.grab().toImage();
    check(image.pixelColor(image.width() / 2, image.height() / 2) ==
              QColor(Qt::red),
          "Mock frame rendered");
    check(image.pixelColor(5, 5) != QColor(Qt::red),
          "Preview preserves aspect ratio");
    preview.clear();
    check(!preview.grab().isNull(), "Inactive preview renders");
    std::optional<Region> selected;
    auto *selector = new chess_eval::ui::RegionSelector(
        {7, "Test", {0, 0, 640, 480}, 2}, nullptr,
        [&](Region r) { selected = r; });
    selector->show();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 200),
                      QPointF(200, 200), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(50, 50),
                        QPointF(50, 50), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(selector, &press);
    QApplication::sendEvent(selector, &release);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(selector, &enter);
    check(selected && selected->rect == QRectF(50, 50, 150, 150),
          "Reverse drag confirmation normalized");
    MockBackend *panelBackend = nullptr;
    chess_eval::ui::CapturePanel panel(nullptr, [&](auto inbox) {
      auto backend = std::make_unique<MockBackend>(inbox);
      panelBackend = backend.get();
      return backend;
    });
    panel.show();
    panelBackend->mailbox->displays(
        {{7, "Test display", QRectF(app.primaryScreen()->geometry()), 1}});
    auto button = [&](const QString &text) {
      for (auto *candidate : panel.findChildren<QPushButton *>())
        if (candidate->text() == text)
          return candidate;
      throw std::runtime_error("Missing capture button");
    };
    auto *selectButton = button("Select Region");
    auto *startButton = button("Start Capture");
    waitFor([&] { return selectButton->isEnabled(); });
    selectButton->click();
    chess_eval::ui::RegionSelector *panelSelector = nullptr;
    for (auto *widget : panel.findChildren<QWidget *>())
      if (auto *candidate =
              dynamic_cast<chess_eval::ui::RegionSelector *>(widget))
        panelSelector = candidate;
    check(panelSelector != nullptr, "Panel opened region selector");
    QApplication::sendEvent(panelSelector, &press);
    QApplication::sendEvent(panelSelector, &release);
    QApplication::sendEvent(panelSelector, &enter);
    waitFor([&] { return startButton->isEnabled(); });
    startButton->click();
    check(panelBackend->starts == 1,
          "Confirmed region enables Start Capture and reaches backend");
    panelBackend->mailbox->event({panelBackend->session, Event::Started, {}});
    panel.shutdown();
    panelBackend->mailbox->event({panelBackend->session, Event::Ended, {}});
    waitFor([&] { return !panel.busy(); });
    std::cout << "Capture geometry, metadata, aspect ratio, state, mailbox, "
                 "manager, preview and selector tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
