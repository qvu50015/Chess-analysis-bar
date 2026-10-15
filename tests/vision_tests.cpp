#include "chess_eval/ui/recognition_controller.hpp"
#include "chess_eval/ui/recognition_panel.hpp"
#include "chess_eval/vision/qt_image.hpp"
#include "chess_eval/vision/recognizer.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <thread>
using namespace chess_eval::vision;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught, "Expected rejection");
}
void waitFor(const std::function<bool()> &predicate) {
  QElapsedTimer t;
  t.start();
  while (!predicate() && t.elapsed() < 5000) {
    QEventLoop loop;
    QTimer::singleShot(5, &loop, &QEventLoop::quit);
    loop.exec();
  }
  check(predicate(), "Async timeout");
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  try {
    check(argc == 2, "Fixture directory required");
    std::filesystem::path root(argv[1]);
    QTemporaryDir temp;
    check(temp.isValid(), "Temporary templates directory");
    std::string labels, expected;
    std::ifstream(root / "synthetic-calibration.labels") >> labels;
    std::ifstream(root / "placement.txt") >> expected;
    auto calibration =
        cv::imread((root / "synthetic-calibration.png").string());
    exportTemplates(calibration, parsePlacement(labels),
                    Orientation::WhiteBottom, temp.path().toStdString());
    TemplateClassifier classifier(temp.path().toStdString());
    const auto truth = parsePlacement(expected);
    const auto calibrationSquares = extractSquares(preprocess(calibration));
    const auto calibrationLabels = parsePlacement(labels);
    for (int i = 0; i < 64; ++i)
      check(classifier.classify(calibrationSquares[i], (i / 8 + i % 8) % 2)
                    .piece == calibrationLabels.squares[i].piece,
            "All 13 classes on both backgrounds");
    // Slight capture/grid misalignment must not turn known pieces into Unknown.
    for (int i = 0; i < 64; ++i) {
      cv::Mat shifted;
      const cv::Matx23d transform(1, 0, 2, 0, 1, -2);
      cv::warpAffine(calibrationSquares[i], shifted, transform, {48, 48},
                     cv::INTER_NEAREST, cv::BORDER_REPLICATE);
      check(classifier.classify(shifted, (i / 8 + i % 8) % 2).piece ==
                calibrationLabels.squares[i].piece,
            "Small alignment offsets preserve classification");
    }
    QTemporaryDir incomplete;
    rejects(
        [&] { TemplateClassifier missing(incomplete.path().toStdString()); });
    QTemporaryDir ambiguous;
    for (const auto &entry :
         std::filesystem::directory_iterator(temp.path().toStdString()))
      std::filesystem::copy_file(
          entry.path(), std::filesystem::path(ambiguous.path().toStdString()) /
                            entry.path().filename());
    const auto ambiguousRoot =
        std::filesystem::path(ambiguous.path().toStdString());
    std::filesystem::copy_file(
        ambiguousRoot / "wQ_l_0.png", ambiguousRoot / "wK_l_0.png",
        std::filesystem::copy_options::overwrite_existing);
    TemplateClassifier tied(ambiguousRoot);
    check(tied.classify(cv::imread((ambiguousRoot / "wQ_l_0.png").string()),
                        false)
                  .piece == '?',
          "Identical competing templates are unknown");
    int correct = 0, total = 0, boards = 0;
    double latency = 0;
    for (auto orientation :
         {Orientation::WhiteBottom, Orientation::BlackBottom}) {
      auto image = cv::imread((root / (orientation == Orientation::WhiteBottom
                                           ? "synthetic-white.png"
                                           : "synthetic-black-retina.png"))
                                  .string());
      auto result = recognize(image, orientation, classifier);
      latency += result.milliseconds;
      for (int i = 0; i < 64; ++i) {
        correct += result.board.squares[i].piece == truth.squares[i].piece;
        ++total;
      }
      boards += result.fenPlacement == expected;
      if (result.fenPlacement != expected) {
        for (int i = 0; i < 64; ++i)
          if (result.board.squares[i].piece != truth.squares[i].piece)
            std::cerr << squareName(i) << " expected " << truth.squares[i].piece
                      << " got " << result.board.squares[i].piece
                      << " confidence " << result.board.squares[i].confidence
                      << '\n';
      }
      check(result.fenPlacement == expected,
            "Labeled board placement must match");
      auto squares = extractSquares(preprocess(image));
      check(squares.size() == 64 && squares[0].size() == cv::Size(48, 48),
            "64 normalized squares");
    }
    std::cout << "Synthetic-only accuracy: " << correct << '/' << total
              << " squares; " << boards
              << "/2 boards. Mean recognition latency " << latency / 2
              << " ms\n";
    check(canonicalIndex(0, 0, Orientation::WhiteBottom) == 0 &&
              canonicalIndex(0, 0, Orientation::BlackBottom) == 63,
          "Orientation mapping");
    check(squareName(0) == "a8" && squareName(63) == "h1", "Coordinates");
    check(!validGeometry(300, 400) && !validGeometry(0, 0) &&
              validGeometry(256, 256),
          "Geometry rejects distorted input");
    rejects([&] { preprocess({}); });
    rejects([&] { preprocess(cv::Mat(50, 50, CV_8UC3)); });
    rejects([&] { preprocess(cv::imread((root / "missing.png").string())); });
    const auto corrupt =
        std::filesystem::path(temp.path().toStdString()) / "corrupt.png";
    std::ofstream(corrupt) << "not an image";
    rejects([&] { preprocess(cv::imread(corrupt.string())); });
    auto unknown = classifier.classify(
        cv::Mat(48, 48, CV_8UC3, cv::Scalar(255, 0, 255)), false);
    check(unknown.piece == '?', "Unrelated pixels unknown");
    TemplateClassifier strict(temp.path().toStdString(), {1, 1});
    check(strict.classify(extractSquares(preprocess(calibration))[0], false)
                  .piece == '?',
          "Ambiguity margin enforced");
    auto invalid = truth;
    invalid.squares[4] = {'.', 1};
    check(!placement(invalid), "Missing king rejected");
    invalid = truth;
    invalid.squares[20] = {'K', 1};
    check(!placement(invalid), "Multiple kings rejected");
    invalid = truth;
    invalid.squares[20] = {'P', 1};
    check(!placement(invalid), "Impossible counts rejected");
    invalid = truth;
    invalid.squares[57] = {'Q', 1};
    check(!placement(invalid),
          "Extra queen with all eight pawns is impossible");
    invalid = truth;
    invalid.squares[0] = {'P', 1};
    check(!placement(invalid), "Pawn on last rank rejected");
    invalid = truth;
    invalid.squares[20] = {'?', 0};
    check(!placement(invalid), "Unknown prevents placement");
    rejects([] { parsePlacement("8/8"); });
    // Padded RGB rows and alternate Qt formats must preserve color and
    // ownership.
    QImage rgb(131, 131, QImage::Format_RGB888);
    rgb.fill(QColor(10, 20, 30));
    auto bgr = toBgr(rgb);
    check(bgr.at<cv::Vec3b>(130, 130) == cv::Vec3b(30, 20, 10),
          "Row stride conversion");
    check(
        toBgr(rgb.convertToFormat(QImage::Format_ARGB32)).at<cv::Vec3b>(0, 0) ==
            cv::Vec3b(30, 20, 10),
        "ARGB conversion");
    rejects([] { toBgr({}); });
    using namespace chess_eval::ui;
    std::atomic_int calls{0};
    RecognitionController controller(
        nullptr,
        [&](const RecognitionRequest &, const std::function<bool()> &) {
          ++calls;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          RecognitionResult r;
          r.milliseconds = calls;
          return r;
        });
    int results = 0, beats = 0;
    double last = 0;
    QObject::connect(&controller, &RecognitionController::completed, &app,
                     [&](const RecognitionResult &r) {
                       ++results;
                       last = r.milliseconds;
                     });
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++beats; });
    heartbeat.start(5);
    controller.submit({rgb, {}, Orientation::WhiteBottom, {}});
    waitFor([&] { return calls == 1; });
    for (int i = 0; i < 20; ++i)
      controller.submit({rgb, {}, Orientation::WhiteBottom, {}});
    waitFor([&] { return results == 1; });
    check(calls == 2 && last == 2,
          "Bounded latest request and stale rejection");
    check(beats > 10, "GUI responsive");
    controller.submit({rgb, {}, Orientation::WhiteBottom, {}});
    waitFor([&] { return calls == 3; });
    controller.invalidate();
    controller.shutdown();
    waitFor([&] { return !controller.running(); });
    check(results == 1, "Shutdown discards stale result");
    RecognitionPanel panel;
    panel.resize(620, 680);
    panel.show();
    app.processEvents();
    check(!panel.grab().isNull(), "Recognition panel renders");
    auto *directory = panel.findChild<QLineEdit *>("templateDirectory");
    auto *output = panel.findChild<QLineEdit *>("recognizedPlacement");
    auto *recognizeButton = panel.findChild<QPushButton *>("recognizeButton");
    check(directory && output && recognizeButton,
          "Recognition controls available");
    recognizeButton->click();
    check(output->text().isEmpty(), "No frame cannot produce a placement");
    directory->setText(temp.path());
    QImage saved(
        QString::fromStdString((root / "synthetic-white.png").string()));
    chess_eval::capture::Frame captured{
        saved.convertToFormat(QImage::Format_ARGB32),
        {{1, "fixture", {0, 0, 384, 384}, 1}, {0, 0, 384, 384}},
        1,
        100};
    panel.setFrame(captured);
    recognizeButton->click();
    waitFor([&] { return output->text().toStdString() == expected; });
    panel.grab().save("recognition-preview.png");
    panel.findChild<QComboBox *>("recognitionProfile")->setCurrentIndex(1);
    check(output->text().isEmpty() && directory->text() != temp.path(),
          "Profile switch clears old result and separates templates");
    panel.findChild<QComboBox *>("recognitionProfile")->setCurrentIndex(0);
    check(directory->text() == temp.path(),
          "Profile-specific directory retained");
    panel.clearFrame();
    check(output->text().isEmpty(),
          "Capture session invalidation clears placement");
    panel.shutdown();
    waitFor([&] { return !panel.busy(); });
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
