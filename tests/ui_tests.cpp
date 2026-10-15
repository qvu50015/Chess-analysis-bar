#include "chess_eval/ui/controller.hpp"
#include "chess_eval/ui/evaluation_bar.hpp"
#include "chess_eval/ui/window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
using namespace chess_eval;
using namespace chess_eval::ui;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void waitFor(const std::function<bool()> &predicate, int timeout = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QEventLoop loop;
    QTimer::singleShot(5, &loop, &QEventLoop::quit);
    loop.exec();
  }
  check(predicate(), "Timed out waiting for GUI state");
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  try {
    check(argc == 2, "Fake engine path required");
    check(displayScore({ScoreKind::centipawns, 32}) == "+0.32",
          "Positive pawn display");
    check(displayScore({ScoreKind::centipawns, -150}) == "-1.50",
          "Negative pawn display");
    check(displayScore({ScoreKind::centipawns, 0}) == "+0.00", "Zero display");
    check(displayScore({ScoreKind::mate, 3}) == "+M3", "Mate display");
    check(displayScore({ScoreKind::mate, 0, Bound::exact, true}) ==
              QString::fromUtf8("−M0"),
          "Checkmate display");
    double previous = -1;
    for (int cp = -10000; cp <= 10000; cp += 25) {
      const double share = white_share({ScoreKind::centipawns, cp});
      check(share >= 0 && share <= 1 && share > previous,
            "Bar mapping bounded and monotonic");
      check(std::abs(share + white_share({ScoreKind::centipawns, -cp}) - 1) <
                1e-12,
            "Bar symmetry");
      previous = share;
    }
    check(white_share({ScoreKind::centipawns, 0}) == 0.5, "Centered zero");
    check(white_share({ScoreKind::mate, -2}) == 0 &&
              white_share({ScoreKind::mate, 2}) == 1,
          "Mate extremes");
    EvaluationBar bar;
    bar.show();
    bar.setEvaluation(0.8, "+5.55");
    waitFor([&] { return bar.share() > 0.5; });
    check(bar.share() < 0.8, "Animation has intermediate values");
    waitFor([&] { return std::abs(bar.share() - 0.8) < 1e-8; });
    auto image = bar.grab().toImage();
    check(image.pixelColor(10, 10).lightness() < 50, "Black at top");
    check(image.pixelColor(10, image.height() - 10).lightness() > 240,
          "White at bottom");
    bar.grab().save("qt-bar-preview.png");
    bar.resize(120, 600);
    check(bar.size() == QSize(120, 600), "Resizable bar");
    bar.setEvaluation(0.1, "-8.00");
    bar.invalidate();
    check(bar.accessibleDescription() == "No current evaluation",
          "Invalidate stale score");
    Controller controller;
    EngineOptions options{argv[1], 4, std::chrono::milliseconds(5000)};
    const QString white = "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    const QString black = "4k3/8/8/8/8/8/8/4K3 b - - 0 1";
    int results = 0, beats = 0;
    QString text, status;
    QObject::connect(&controller, &Controller::evaluated, &app,
                     [&](double, const QString &value) {
                       ++results;
                       text = value;
                     });
    QObject::connect(&controller, &Controller::statusChanged, &app,
                     [&](const QString &value) { status = value; });
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++beats; });
    heartbeat.start(10);
    qputenv("CHESS_EVAL_TEST_MODE", "slow");
    controller.submit(white, options);
    check(!controller.busy(), "Debounce delays launch");
    waitFor([&] { return controller.busy(); });
    controller.submit(black, options);
    waitFor([&] { return results == 1 && !controller.busy(); });
    check(text == "-0.31", "Old generation cannot overwrite new evaluation");
    check(beats > 30, "GUI event loop responsive during analysis");
    controller.submit(black, options);
    check(!controller.busy(), "Unchanged position not reanalyzed");
    for (const auto *mode : {"search_hang", "hang"}) {
      qputenv("CHESS_EVAL_TEST_MODE", mode);
      controller.stop();
      controller.submit(white, options);
      waitFor([&] { return controller.busy(); });
      QElapsedTimer running;
      running.start();
      waitFor([&] { return running.elapsed() >= 150; });
      QElapsedTimer stopped;
      stopped.start();
      controller.stop();
      waitFor([&] { return !controller.busy(); });
      check(stopped.elapsed() < 2000 && results == 1,
            "Stop cancels startup/search without result");
      check(waitpid(-1, nullptr, WNOHANG) == -1, "Cancelled child reaped");
    }
    controller.submit("invalid", options);
    check(status.startsWith("Invalid FEN") && !controller.busy(),
          "Invalid FEN rejected");
    qputenv("CHESS_EVAL_TEST_MODE", "normal");
    controller.submit(white, options);
    waitFor([&] { return results == 2 && !controller.busy(); });
    check(text == "+0.31", "Restart after stop/error");
    options.executable = "/nonexistent/stockfish";
    controller.submit(white, options);
    waitFor([&] {
      return status.startsWith("Cannot launch") && !controller.busy();
    });
    Window window;
    window.show();
    app.processEvents();
    check(!window.grab().isNull(), "Settings window renders");
    window.grab().save("qt-settings-preview.png");
    window.findChild<QLineEdit *>("engineInput")->setText(argv[1]);
    qputenv("CHESS_EVAL_TEST_MODE", "search_hang");
    window.findChild<QPushButton *>("startButton")->click();
    QElapsedTimer running;
    running.start();
    waitFor([&] { return running.elapsed() >= 600; });
    const int beforeClose = beats;
    window.close();
    waitFor([&] { return !window.isVisible(); });
    check(beats > beforeClose,
          "Closing during analysis leaves event loop responsive");
    check(waitpid(-1, nullptr, WNOHANG) == -1, "Window close reaps engine");
    std::cout << "Qt mapping, display, animation, debounce, stale results, "
                 "responsiveness, stop and restart tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
