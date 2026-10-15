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
    // Last-move backgrounds change while the piece artwork stays the same.
    for (bool selection : {false, true}) {
      for (int i = 0; i < 64; ++i) {
        auto highlighted = calibrationSquares[i].clone();
        const auto background = highlighted.at<cv::Vec3b>(3, 3);
        for (int y = 0; y < highlighted.rows; ++y)
          for (int x = 0; x < highlighted.cols; ++x) {
            auto &pixel = highlighted.at<cv::Vec3b>(y, x);
            if (cv::norm(pixel, background, cv::NORM_INF) <= 24)
              pixel = cv::Vec3b(background[0] - 40, background[1] + 45,
                                cv::saturate_cast<unsigned char>(
                                    background[2] + (selection ? -60 : 40)));
          }
        check(classifier.classify(highlighted, (i / 8 + i % 8) % 2).piece ==
                  calibrationLabels.squares[i].piece,
              "Selection and move-highlight backgrounds preserve every class "
              "on both colors");
      }
    }
    // Actual user screenshot: green legal-target dots on both board colors.
    QTemporaryDir dotTemplates;
    for (const auto &entry :
         std::filesystem::directory_iterator(temp.path().toStdString()))
      std::filesystem::copy_file(
          entry.path(),
          std::filesystem::path(dotTemplates.path().toStdString()) /
              entry.path().filename());
    auto dots = cv::imread((root / "legal-dots.png").string());
    check(!dots.empty(), "Legal-dot screenshot fixture");
    std::array<cv::Mat, 2> dotSquares;
    for (int side = 0; side < 2; ++side) {
      cv::resize(dots(cv::Rect(side ? 148 : 6, 0, 142, 142)), dotSquares[side],
                 {48, 48}, 0, 0, cv::INTER_AREA);
      cv::GaussianBlur(dotSquares[side], dotSquares[side], {3, 3}, 0.5);
      auto color = dotSquares[side].at<cv::Vec3b>(3, 3);
      cv::Mat empty(48, 48, CV_8UC3, cv::Scalar(color[0], color[1], color[2]));
      cv::imwrite((std::filesystem::path(dotTemplates.path().toStdString()) /
                   (side ? "ee_d_0.png" : "ee_l_0.png"))
                      .string(),
                  empty);
    }
    TemplateClassifier dotClassifier(dotTemplates.path().toStdString());
    for (int side = 0; side < 2; ++side) {
      check(dotClassifier.classify(dotSquares[side], side).piece == '.',
            "Real legal-target dot is recognized as empty on both colors");
      for (double offset : {-4.5, 4.5}) {
        cv::Mat shifted;
        cv::warpAffine(dotSquares[side], shifted,
                       cv::Matx23d(1, 0, offset, 0, 1, -offset), {48, 48},
                       cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        check(dotClassifier.classify(shifted, side).piece == '.',
              "Real legal-target dots tolerate fractional crop offsets");
      }
      auto obstruction = dotSquares[side].clone();
      cv::rectangle(obstruction, {15, 30, 18, 9}, cv::Scalar(0, 0, 0),
                    cv::FILLED);
      check(dotClassifier.classify(obstruction, side).piece != '.',
            "A dot with other foreground must not hide an occupied square");
      auto nonGreen = dotSquares[side].clone();
      cv::circle(nonGreen, {24, 24}, 7, cv::Scalar(255, 0, 255), cv::FILLED);
      check(dotClassifier.classify(nonGreen, side).piece != '.',
            "An arbitrary colored disk is not erased as a legal indicator");
    }
    for (int i = 0; i < 64; ++i) {
      if (calibrationLabels.squares[i].piece == '.')
        continue;
      auto covered = calibrationSquares[i].clone();
      cv::circle(covered, {24, 24}, 7, cv::Scalar(70, 140, 100), cv::FILLED);
      check(classifier.classify(covered, (i / 8 + i % 8) % 2).piece != '.',
            "A green dot over piece artwork must never erase the piece");
    }
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
    // Exercise the capture path, including fractional alignment, scale drift,
    // and isolated damaged pixels rather than only integer square translations.
    const auto cleanBoard = cv::imread((root / "synthetic-white.png").string());
    for (double scale : {0.99, 1.0, 1.01}) {
      for (double offset : {-4.5, 4.5}) {
        cv::Mat altered;
        const cv::Matx23d transform(scale, 0, 192 * (1 - scale) + offset, 0,
                                    scale, 192 * (1 - scale) - offset);
        cv::warpAffine(cleanBoard, altered, transform, cleanBoard.size(),
                       cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        cv::RNG noise(42);
        for (int n = 0; n < 192; ++n)
          altered.at<cv::Vec3b>(noise.uniform(0, altered.rows),
                                noise.uniform(0, altered.cols)) = {255, 0, 255};
        check(recognize(altered, Orientation::WhiteBottom, classifier)
                      .fenPlacement == expected,
              "Small crop shifts, scale drift and pixel noise preserve board");
      }
    }
    // Run legal dots through whole-board resize, blur and square extraction,
    // including a crop that lets neighboring backgrounds enter square edges.
    auto dottedBoard = cleanBoard.clone();
    for (int index : {36, 44})
      cv::circle(dottedBoard, {index % 8 * 48 + 24, index / 8 * 48 + 24}, 7,
                 cv::Scalar(63, 113, 105), cv::FILLED, cv::LINE_AA);
    for (double offset : {-4.5, 0.0, 4.5}) {
      cv::Mat captured;
      cv::warpAffine(dottedBoard, captured,
                     cv::Matx23d(1, 0, offset, 0, 1, -offset),
                     cleanBoard.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
      cv::resize(captured, captured, {1148, 1148}, 0, 0, cv::INTER_LINEAR);
      auto capturedResult =
          recognize(captured, Orientation::WhiteBottom, classifier);
      check(capturedResult.fenPlacement == expected,
            "Whole-board capture recognizes legal dots with crop offsets");
    }
    // Full saved capture with Black at the bottom, selected d7 pawn and
    // legal-target dots on d6/d5. Keep the user's real calibration separate
    // from synthetic glyph tests; no templates are derived from this image.
    TemplateClassifier selectedClassifier(root / "lichess-user-profile");
    const auto selectedCapture =
        cv::imread((root / "lichess-selected-pawn.png").string());
    auto selectedResult = recognize(selectedCapture, Orientation::BlackBottom,
                                    selectedClassifier);
    check(selectedResult.fenPlacement ==
              "rnbqkbnr/pppppppp/8/8/5P2/8/PPPPP1PP/RNBQKBNR",
          "Actual selected-piece snapshot recognizes every square with saved "
          "Lichess calibration");
    check(selectedResult.board.squares[11].piece == 'p' &&
              selectedResult.board.squares[19].piece == '.' &&
              selectedResult.board.squares[27].piece == '.',
          "Green highlighted d7 pawn and d6/d5 legal dots retain their "
          "identities");
    const auto premoveCapture =
        cv::imread((root / "lichess-premove-dots.png").string());
    const auto premoveResult =
        recognize(premoveCapture, Orientation::WhiteBottom, selectedClassifier);
    check(
        premoveResult.fenPlacement ==
            "rnbqkbnr/pppppppp/8/8/5P2/8/PPPPP1PP/RNBQKBNR",
        "Actual premove snapshot recognizes gray dots and selected white pawn");
    const auto premoveSquares = extractSquares(preprocess(premoveCapture));
    for (int index : {35, 43}) {
      for (double offset : {-4.5, 0.0, 4.5}) {
        cv::Mat shifted;
        cv::warpAffine(premoveSquares[index], shifted,
                       cv::Matx23d(1, 0, offset, 0, 1, -offset), {48, 48},
                       cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        check(selectedClassifier.classify(shifted, (index / 8 + index % 8) % 2)
                      .piece == '.',
              "Gray premove dots survive crop offsets on both square colors");
      }
      auto obstructed = premoveSquares[index].clone();
      cv::rectangle(obstructed, {15, 30, 18, 9}, cv::Scalar(0, 0, 0),
                    cv::FILLED);
      check(selectedClassifier.classify(obstructed, (index / 8 + index % 8) % 2)
                    .piece != '.',
            "Gray dot with other foreground cannot hide an occupied square");
    }
    for (int i = 0; i < 64; ++i) {
      if (calibrationLabels.squares[i].piece == '.')
        continue;
      auto covered = calibrationSquares[i].clone();
      cv::circle(covered, {24, 24}, 7, cv::Scalar(110, 95, 105), cv::FILLED);
      check(classifier.classify(covered, (i / 8 + i % 8) % 2).piece != '.',
            "Gray dot over piece artwork must never erase the piece");
    }
    for (const char *file :
         {"lichess-premove-backgrounds.png", "lichess-selected-queen.png"}) {
      const auto capture = cv::imread((root / file).string());
      const auto result =
          recognize(capture, Orientation::WhiteBottom, selectedClassifier);
      check(result.fenPlacement ==
                "rnbqkbnr/ppp1pppp/8/3p4/4P1Q1/8/PPPP1PPP/RNB1KBNR",
            "Selected queen and full-square premove highlights preserve the "
            "actual position");
      const auto squares = extractSquares(preprocess(capture));
      check(selectedClassifier.classify(squares[38], false).piece == 'Q',
            "Queen stays recognized on a selected light square");
      auto obstructed = squares[38].clone();
      cv::rectangle(obstructed, {12, 12, 24, 24}, cv::Scalar(255, 0, 255),
                    cv::FILLED);
      check(selectedClassifier.classify(obstructed, false).piece == '?',
            "Highlight correction must not recover an obscured queen");
    }
    QTemporaryDir incomplete;
    // Real capture regression: these pawns are identical artwork, but board
    // normalization lands their outlines on different fractional pixels.
    QTemporaryDir fractional;
    for (const auto &entry :
         std::filesystem::directory_iterator(temp.path().toStdString()))
      std::filesystem::copy_file(
          entry.path(), std::filesystem::path(fractional.path().toStdString()) /
                            entry.path().filename());
    std::filesystem::copy_file(
        root / "lichess-a2.png",
        std::filesystem::path(fractional.path().toStdString()) / "wP_l_0.png",
        std::filesystem::copy_options::overwrite_existing);
    TemplateClassifier fractionalClassifier(fractional.path().toStdString());
    check(
        fractionalClassifier
                .classify(cv::imread((root / "lichess-e2.png").string()), false)
                .piece == 'P',
        "Real pawn outline survives fractional-pixel alignment");
    auto realHighlighted = cv::imread((root / "lichess-e2.png").string());
    const auto realBackground = realHighlighted.at<cv::Vec3b>(3, 3);
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 48; ++x)
        if (cv::norm(realHighlighted.at<cv::Vec3b>(y, x), realBackground,
                     cv::NORM_INF) <= 24)
          realHighlighted.at<cv::Vec3b>(y, x) = {106, 210, 205};
    check(fractionalClassifier.classify(realHighlighted, false).piece == 'P',
          "Real pawn survives a Lichess-style highlighted background");
    cv::rectangle(realHighlighted, {14, 14, 20, 20}, cv::Scalar(255, 0, 255),
                  cv::FILLED);
    check(fractionalClassifier.classify(realHighlighted, false).piece == '?',
          "An obscured highlighted pawn still freezes recognition");
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
