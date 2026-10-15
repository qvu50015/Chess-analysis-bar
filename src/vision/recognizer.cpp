#include "chess_eval/vision/recognizer.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <stdexcept>
namespace chess_eval::vision {
bool validGeometry(int width, int height) {
  return width >= 128 && height >= 128 && width <= 8192 && height <= 8192 &&
         std::abs(static_cast<double>(width) / height - 1) <= 0.04;
}
cv::Mat preprocess(const cv::Mat &bgr) {
  if (bgr.empty() || bgr.type() != CV_8UC3 ||
      !validGeometry(bgr.cols, bgr.rows))
    throw std::invalid_argument("Board must be a full, nearly square 8x8 crop "
                                "(128–8192 pixels, at most 4% distortion)");
  cv::Mat result;
  cv::resize(bgr, result, {SquarePixels * 8, SquarePixels * 8}, 0, 0,
             cv::INTER_AREA);
  cv::GaussianBlur(result, result, {3, 3}, 0.5);
  return result;
}
std::array<cv::Mat, 64> extractSquares(const cv::Mat &image) {
  if (image.type() != CV_8UC3 ||
      image.size() != cv::Size(SquarePixels * 8, SquarePixels * 8))
    throw std::invalid_argument("Expected normalized board image");
  std::array<cv::Mat, 64> result;
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col)
      result[row * 8 + col] = image(cv::Rect(
          col * SquarePixels, row * SquarePixels, SquarePixels, SquarePixels));
  return result;
}
TemplateClassifier::TemplateClassifier(const std::filesystem::path &directory,
                                       Thresholds thresholds)
    : thresholds_(thresholds) {
  if (!std::isfinite(thresholds.similarity) ||
      !std::isfinite(thresholds.margin) || thresholds.similarity < 0 ||
      thresholds.similarity > 1 || thresholds.margin < 0 ||
      thresholds.margin > 1)
    throw std::invalid_argument("Invalid classifier thresholds");
  if (!std::filesystem::is_directory(directory))
    throw std::invalid_argument(
        "Select a template directory created from your board theme");
  std::map<std::pair<char, bool>, int> coverage;
  for (const auto &file : std::filesystem::directory_iterator(directory)) {
    if (file.path().extension() != ".png")
      continue;
    const auto name = file.path().stem().string();
    if (name.size() < 4 || name[2] != '_' || (name[3] != 'l' && name[3] != 'd'))
      continue;
    if (name[0] != 'w' && name[0] != 'b' && name.substr(0, 2) != "ee")
      continue;
    const char piece = name.substr(0, 2) == "ee" ? '.'
                       : name[0] == 'w'
                           ? name[1]
                           : static_cast<char>(std::tolower(name[1]));
    if (std::string(".PNBRQKpnbrqk").find(piece) == std::string::npos)
      continue;
    if (templates_.size() >= 512)
      throw std::invalid_argument("Template set exceeds 512 images");
    auto image = cv::imread(file.path().string(), cv::IMREAD_COLOR);
    if (image.empty() || image.size() != cv::Size(SquarePixels, SquarePixels))
      throw std::invalid_argument("Invalid template: " + file.path().string());
    const bool dark = name[3] == 'd';
    Entry entry{piece, dark, {}};
    // Cache modest size variants; existing on-disk calibrations remain usable.
    for (int i = 0; i < 3; ++i) {
      const double scale = 1.0 + (i - 1) * 0.03;
      const double center = (SquarePixels - 1) / 2.0;
      const cv::Matx23d transform(scale, 0, center * (1 - scale),
                                 0, scale, center * (1 - scale));
      cv::warpAffine(image, entry.images[i], transform, image.size(),
                     cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    templates_.push_back(std::move(entry));
    ++coverage[{piece, dark}];
  }
  for (char piece : std::string(".PNBRQKpnbrqk"))
    for (bool dark : {false, true})
      if (!coverage.contains({piece, dark}))
        throw std::invalid_argument(
            std::string("Template set incomplete: missing ") + piece +
            (dark ? " on dark squares" : " on light squares"));
}
Square TemplateClassifier::classify(const cv::Mat &square, bool dark) const {
  if (square.type() != CV_8UC3 ||
      square.size() != cv::Size(SquarePixels, SquarePixels))
    return {};
  std::map<char, double> bestByClass;
  for (const auto &entry : templates_) {
    if (entry.dark != dark)
      continue;
    // Compare the central artwork over a wider translation window, excluding
    // the outermost captured pixels where neighboring squares can intrude.
    // All variants compete as one class, so they cannot create false ambiguity.
    constexpr int inset = 8;
    constexpr int side = SquarePixels - 2 * inset;
    for (const auto &image : entry.images) {
      cv::Mat distance;
      cv::matchTemplate(
          square(cv::Rect(1, 1, SquarePixels - 2, SquarePixels - 2)),
          image(cv::Rect(inset, inset, side, side)), distance, cv::TM_SQDIFF);
      double minimum;
      cv::Point location;
      cv::minMaxLoc(distance, &minimum, nullptr, &location);
      // Screen scaling places otherwise identical outlines between normalized
      // pixels. Refine the integer match instead of relaxing confidence limits.
      const auto reference = image(cv::Rect(inset, inset, side, side));
      for (float dy : {-0.5f, 0.0f, 0.5f})
        for (float dx : {-0.5f, 0.0f, 0.5f}) {
          if (dx == 0 && dy == 0)
            continue;
          cv::Mat patch;
          cv::getRectSubPix(square, {side, side},
                            {location.x + 1 + (side - 1) / 2.0f + dx,
                             location.y + 1 + (side - 1) / 2.0f + dy},
                            patch);
          minimum = std::min(minimum, cv::norm(patch, reference, cv::NORM_L2SQR));
        }
      const double mse =
          std::max(0.0, minimum) / (side * side * 3 * 255.0 * 255.0);
      const double similarity = std::clamp(1.0 - std::sqrt(mse), 0.0, 1.0);
      bestByClass[entry.piece] = std::max(bestByClass[entry.piece], similarity);
    }
  }
  char piece = '?';
  double best = -1, second = -1;
  for (const auto &[label, score] : bestByClass) {
    if (score > best) {
      second = best;
      best = score;
      piece = label;
    } else
      second = std::max(second, score);
  }
  if (best < thresholds_.similarity || best - second < thresholds_.margin)
    piece = '?';
  return {piece, std::max(0.0, best)};
}
RecognitionResult recognize(const cv::Mat &bgr, Orientation orientation,
                            const PieceClassifier &classifier,
                            const std::function<bool()> &cancel) {
  const auto start = std::chrono::steady_clock::now();
  auto normalized = preprocess(bgr);
  auto squares = extractSquares(normalized);
  RecognitionResult result;
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) {
      if (cancel && cancel())
        throw std::runtime_error("Recognition cancelled");
      result.board.squares[canonicalIndex(row, col, orientation)] =
          classifier.classify(squares[row * 8 + col], (row + col) % 2 != 0);
    }
  result.errors = validate(result.board);
  result.fenPlacement = placement(result.board);
  result.milliseconds = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start)
                            .count();
  return result;
}
void exportTemplates(const cv::Mat &bgr, const Board &labels,
                     Orientation orientation,
                     const std::filesystem::path &directory) {
  const auto squares = extractSquares(preprocess(bgr));
  std::filesystem::create_directories(directory);
  std::set<std::pair<char, bool>> saved;
  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col) {
      const char piece =
          labels.squares[canonicalIndex(row, col, orientation)].piece;
      if (std::string(".PNBRQKpnbrqk").find(piece) == std::string::npos)
        throw std::invalid_argument("Unknown reference label");
      if (!saved.insert({piece, (row + col) % 2 != 0}).second)
        continue;
      const std::string prefix =
          (piece == '.' ? std::string("ee")
                        : std::string(piece >= 'a' ? "b" : "w") +
                              static_cast<char>(std::toupper(piece))) +
          ((row + col) % 2 ? "_d_" : "_l_");
      int variant = 0;
      std::filesystem::path path;
      do {
        path = directory / (prefix + std::to_string(variant++) + ".png");
      } while (std::filesystem::exists(path));
      if (!cv::imwrite(path.string(), squares[row * 8 + col]))
        throw std::runtime_error("Could not write template image");
    }
}
} // namespace chess_eval::vision
