#include "chess_eval/ui/capture_panel.hpp"
#include "chess_eval/ui/controller.hpp"
#include "chess_eval/ui/evaluation_bar.hpp"
#include "chess_eval/ui/live_controller.hpp"
#include "chess_eval/ui/window.hpp"
#include <QApplication>
#include <QComboBox>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <thread>
using namespace chess_eval;
using namespace chess_eval::ui;
using namespace chess_eval::vision;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void waitFor(const std::function<bool()> &predicate, int timeout = 6000) {
  QElapsedTimer clock;
  clock.start();
  while (!predicate() && clock.elapsed() < timeout) {
    QEventLoop loop;
    QTimer::singleShot(5, &loop, &QEventLoop::quit);
    loop.exec();
  }
  check(predicate(), "Live pipeline timeout");
}
void spin(int ms) {
  QElapsedTimer t;
  t.start();
  waitFor([&] { return t.elapsed() >= ms; }, ms + 1000);
}
capture::Frame frame(const QImage &image, uint64_t session = 1,
                     int64_t time = 1) {
  return {image.convertToFormat(QImage::Format_ARGB32),
          {{1, "fixture", {0, 0, 384, 384}, 1}, {0, 0, 384, 384}},
          session,
          time};
}
QImage render(const std::string &placement,
              const std::filesystem::path &directory, Orientation orientation) {
  auto labels = parsePlacement(placement);
  cv::Mat image(384, 384, CV_8UC3);
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c) {
      char p = labels.squares[canonicalIndex(r, c, orientation)].piece;
      std::string prefix =
          p == '.' ? "ee"
                   : std::string(p >= 'a' ? "b" : "w") + char(std::toupper(p));
      auto tile = cv::imread(
          (directory / (prefix + ((r + c) % 2 ? "_d_0.png" : "_l_0.png")))
              .string());
      tile.copyTo(image(cv::Rect(c * 48, r * 48, 48, 48)));
    }
  return QImage(image.data, image.cols, image.rows, int(image.step),
                QImage::Format_BGR888)
      .copy();
}
QImage highlightMove(QImage image, Orientation orientation, int from, int to) {
  image = image.convertToFormat(QImage::Format_ARGB32);
  for (int index : {from, to}) {
    const int screen =
        orientation == Orientation::WhiteBottom ? index : 63 - index;
    const int left = screen % 8 * 48, top = screen / 8 * 48;
    const auto background = image.pixelColor(left + 3, top + 3);
    for (int y = top; y < top + 48; ++y)
      for (int x = left; x < left + 48; ++x) {
        const auto pixel = image.pixelColor(x, y);
        if (std::max({std::abs(pixel.red() - background.red()),
                      std::abs(pixel.green() - background.green()),
                      std::abs(pixel.blue() - background.blue())}) <= 24)
          image.setPixelColor(x, y,
                              QColor(background.red() + 40,
                                     background.green() + 45,
                                     background.blue() - 40));
      }
  }
  return image;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  try {
    check(argc == 3, "Fixture directory and fake engine required");
    std::filesystem::path root(argv[1]);
    QTemporaryDir temp;
    check(temp.isValid(), "Temporary profile");
    std::string labels;
    std::ifstream(root / "synthetic-calibration.labels") >> labels;
    exportTemplates(cv::imread((root / "synthetic-calibration.png").string()),
                    parsePlacement(labels), Orientation::WhiteBottom,
                    temp.path().toStdString());
    chess::Game initial;
    auto e4 = initial.play({52, 36});
    auto e5 = e4.play({12, 28});
    int beats = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++beats; });
    heartbeat.start(5);
    for (auto orientation :
         {Orientation::WhiteBottom, Orientation::BlackBottom}) {
      LiveController live;
      Controller engine;
      EvaluationBar bar;
      EngineOptions options{argv[2], 4, std::chrono::milliseconds(5000)};
      int confirmed = 0, evaluations = 0;
      QString lastFen, score, status;
      QObject::connect(&live, &LiveController::positionConfirmed, &app,
                       [&](const QString &fen) {
                         ++confirmed;
                         lastFen = fen;
                         engine.submit(fen, options,
                                       AnalysisInput::ConfirmedLive);
                       });
      QObject::connect(&live, &LiveController::suspended, &engine,
                       &Controller::stop);
      QObject::connect(&live, &LiveController::statusChanged, &app,
                       [&](const QString &s) { status = s; });
      QObject::connect(&engine, &Controller::invalidated, &bar,
                       &EvaluationBar::invalidate);
      QObject::connect(&engine, &Controller::evaluated, &app,
                       [&](double share, const QString &s) {
                         ++evaluations;
                         score = s;
                         bar.setEvaluation(share, s);
                       });
      check(!live.watch(temp.path(), Orientation::WhiteBottom, true),
            "Capture required");
      live.setFrame(frame(
          render(initial.placement(), temp.path().toStdString(), orientation)));
      qputenv("CHESS_EVAL_TEST_MODE", "search_hang");
      check(live.watch(temp.path(), Orientation::WhiteBottom, true),
            "Live start");
      waitFor([&] { return confirmed == 1 && engine.busy(); });
      // An unknown animation cancels a running old search before a new legal
      // board.
      QImage animation(384, 384, QImage::Format_ARGB32);
      animation.fill(Qt::magenta);
      live.setFrame(frame(animation, 1, 2));
      waitFor([&] { return status.contains("frozen") && !engine.busy(); });
      check(evaluations == 0, "Old search never publishes through uncertainty");
      qputenv("CHESS_EVAL_TEST_MODE", "normal");
      live.setFrame(
          frame(highlightMove(render(e4.placement(), temp.path().toStdString(),
                                     orientation),
                              orientation, 52, 36),
                1, 3));
      waitFor([&] { return evaluations == 1; });
      check(
          lastFen.toStdString() == e4.fen() && score == "-0.31",
          "Capture to recognition to tracked FEN to white-relative evaluation");
      spin(600);
      check(confirmed == 2 && evaluations == 1,
            "Unchanged board not reanalyzed");
      live.setFrame(
          frame(highlightMove(render(e5.placement(), temp.path().toStdString(),
                                     orientation),
                              orientation, 12, 28),
                1, 4));
      waitFor([&] { return evaluations == 2; });
      check(lastFen.toStdString() == e5.fen() && score == "+0.31",
            "Next completed move updates only current score");
      // Both plies arrive before another observation: recover the move/reply
      // directly from the latest captured board, then continue normally.
      auto nf3 = e5.play({62, 45});
      auto nc6 = nf3.play({1, 18});
      live.setFrame(
          frame(render(nc6.placement(), temp.path().toStdString(), orientation),
                1, 5));
      waitFor([&] { return evaluations == 3; });
      check(lastFen.toStdString() == nc6.fen() && confirmed == 4,
            "Skipped intermediate board recovers and automatically evaluates");
      live.setFrame(frame(
          render(initial.placement(), temp.path().toStdString(), orientation),
          1, 8));
      check(live.watch(temp.path(), Orientation::WhiteBottom, true),
            "Start fast-game recovery test");
      waitFor([&] { return evaluations == 4; });
      auto skipped = e4.play({11, 27}).play({36, 27}).play({3, 27});
      const int heartbeatBefore = beats;
      live.setFrame(frame(
          render(skipped.placement(), temp.path().toStdString(), orientation),
          1, 9));
      waitFor([&] { return evaluations == 5; });
      check(
          live.active() && lastFen.toStdString() == skipped.fen() &&
              score == "+0.31" && beats > heartbeatBefore + 5,
          "Four rapid plies recover and evaluate without Reset or blocking Qt");
      spin(300);
      check(evaluations == 5, "Recovered unchanged board is not reanalyzed");
      live.stop();
      check(live.watch(temp.path(), Orientation::WhiteBottom),
            "Resume detected orientation");
      waitFor([&] { return evaluations == 6; });
      check(lastFen.toStdString() == skipped.fen(),
            "Stop/Start retains automatic orientation and game state");
      const auto opposite = orientation == Orientation::WhiteBottom
                                ? Orientation::BlackBottom
                                : Orientation::WhiteBottom;
      live.setFrame(frame(
          render(initial.placement(), temp.path().toStdString(), opposite), 1,
          10));
      check(live.watch(temp.path(), Orientation::WhiteBottom, true),
            "Reset for other color");
      waitFor([&] { return evaluations == 7; });
      check(lastFen.toStdString() == initial.fen(),
            "Reset redetects orientation when switching player colors");
      live.clearFrame();
      check(!live.active() &&
                bar.accessibleDescription() == "No current evaluation",
            "Capture loss freezes evaluation");
      live.shutdown();
      engine.stop();
      waitFor([&] { return !live.busy() && !engine.busy(); });
    }
    LiveController invalid;
    QString failure;
    QObject::connect(&invalid, &LiveController::statusChanged, &app,
                     [&](const QString &message) { failure = message; });
    invalid.setFrame(
        frame(render(initial.placement(), temp.path().toStdString(),
                     Orientation::WhiteBottom)));
    check(!invalid.start("bad FEN", temp.path(), Orientation::WhiteBottom),
          "Invalid starting FEN rejected before recognition");
    invalid.start(chess::Game::InitialFen, temp.path() + "/missing",
                  Orientation::WhiteBottom);
    waitFor([&] { return !invalid.active(); });
    check(failure.contains("template directory"),
          "Template errors pause live analysis");
    invalid.shutdown();
    waitFor([&] { return !invalid.busy(); });
    // Slow recognition: new frames replace one mailbox, never spawn or queue
    // work per frame. Stop/restart must reject the old worker's eventual
    // result.
    std::atomic_int calls{0};
    LiveController slow(nullptr, [&](const RecognitionRequest &request,
                                     const std::function<bool()> &) {
      ++calls;
      std::this_thread::sleep_for(std::chrono::milliseconds(180));
      RecognitionResult result;
      int marker = request.image.pixelColor(0, 0).red();
      if (marker == 1)
        result.fenPlacement = initial.placement();
      if (marker == 2)
        result.fenPlacement = e4.placement();
      if (marker == 3)
        result.fenPlacement = e5.placement();
      return result;
    });
    auto marked = [&](int marker, int64_t time) {
      QImage image(384, 384, QImage::Format_ARGB32);
      image.fill(QColor(marker, 0, 0));
      return frame(image, 1, time);
    };
    int publications = 0;
    QString status;
    QObject::connect(&slow, &LiveController::positionConfirmed, &app,
                     [&](const QString &) { ++publications; });
    QObject::connect(&slow, &LiveController::statusChanged, &app,
                     [&](const QString &s) { status = s; });
    slow.setFrame(marked(1, 1));
    slow.start(chess::Game::InitialFen, "profile-a", Orientation::WhiteBottom);
    waitFor([&] { return calls >= 1; });
    slow.stop();
    spin(300);
    check(publications == 0, "Stop discards in-flight recognition");
    slow.start(chess::Game::InitialFen, "profile-b", Orientation::BlackBottom);
    waitFor([&] { return publications == 1; });
    int before = calls;
    for (int n = 2; n < 102; ++n)
      slow.setFrame(marked(n == 101 ? 3 : 2, n));
    waitFor([&] { return publications == 2; });
    check(slow.active(), "Rapid move and reply recover without restarting");
    check(calls - before < 16, "Recognition queue bounded and rate limited");
    slow.setFrame(marked(1, 103));
    slow.start(chess::Game::InitialFen, "profile-a", Orientation::WhiteBottom);
    waitFor([&] { return publications == 3; });
    slow.setFrame(marked(2, 104));
    waitFor([&] { return publications == 4; });
    slow.stop();
    // Resume on the opponent's reply, without seeing the previous placement
    // again. The worker must receive the retained, synchronized tracker.
    slow.setFrame(marked(3, 105));
    check(slow.watch("profile-a", Orientation::WhiteBottom),
          "Resume without FEN");
    waitFor([&] { return publications == 5; });
    slow.setFrame(marked(0, 106));
    spin(3300);
    check(slow.active() && publications == 5,
          "Unreadable board does not stop live mode");
    slow.setFrame(marked(3, 107));
    waitFor([&] { return publications == 6; });
    slow.setFrame(marked(1, 108));
    slow.watch("profile-a", Orientation::WhiteBottom, true);
    waitFor([&] { return publications == 7; });
    auto changed = marked(1, 109);
    changed.session = 2;
    slow.setFrame(changed);
    check(!slow.active(), "New capture session requires resync");
    slow.shutdown();
    waitFor([&] { return !slow.busy(); });
    // Exercise the actual Window wiring and both existing profile selectors.
    Window window;
    window.show();
    window.findChild<QLineEdit *>("engineInput")->setText(argv[2]);
    auto *capture = window.findChild<CapturePanel *>();
    auto *profile = window.findChild<QComboBox *>("recognitionProfile");
    auto *directory = window.findChild<QLineEdit *>("templateDirectory");
    auto *start = window.findChild<QPushButton *>("startLiveButton");
    auto *stop = window.findChild<QPushButton *>("stopLiveButton");
    auto *liveStatus = window.findChild<QLabel *>("liveStatus");
    auto *bar = window.findChild<QWidget *>("evaluationBar");
    check(!window.findChild<QComboBox *>("liveMode") &&
              !window.findChild<QComboBox *>("liveSideToMove"),
          "Current Board and turn controls removed");
    check(
        !window.findChild<QLineEdit *>("liveStartingFen") &&
            window.findChild<QPushButton *>("newLiveGameButton"),
        "Live controls require no FEN and provide an explicit new game reset");
    check(capture && profile && directory && start && stop && liveStatus && bar,
          "Live UI controls available");
    for (int selected : {0, 1}) {
      profile->setCurrentIndex(selected);
      directory->setText(temp.path());
      capture->frameAvailable(
          frame(render(initial.placement(), temp.path().toStdString(),
                       selected ? Orientation::BlackBottom
                                : Orientation::WhiteBottom),
                uint64_t(selected + 1)));
      start->click();
      check(!start->isEnabled() && stop->isEnabled(), "Active controls");
      waitFor([&] {
        return bar->accessibleDescription().startsWith("White evaluation");
      });
      stop->click();
      check(start->isEnabled() && !stop->isEnabled() &&
                bar->accessibleDescription() == "No current evaluation",
            "Stop controls and score invalidation");
    }
    start->click();
    profile->setCurrentIndex(0);
    check(start->isEnabled() && liveStatus->text().contains("settings changed"),
          "Profile changes stop live session");
    window.findChild<QTabWidget *>()->setCurrentIndex(3);
    app.processEvents();
    window.grab().save("live-standard-preview.png");
    window.close();
    waitFor([&] { return !window.isVisible(); });
    check(beats > 100,
          "GUI stays responsive during recognition and engine work");
    std::cout
        << "Live capture/recognition/tracking/engine pipeline, orientations, "
           "profiles, cancellation, rapid changes and UI passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
