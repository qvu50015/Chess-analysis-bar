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
namespace {
std::optional<cv::Vec3b> squareBackground(const cv::Mat &image, int inset = 2) {
  // Sample four corner patches away from the outer border. Median and consensus
  // tolerate coordinate labels, but reject mixed corners or an obscured square.
  std::vector<cv::Vec3b> pixels;
  for (int top : {inset, SquarePixels - inset - 4})
    for (int left : {inset, SquarePixels - inset - 4})
      for (int y = top; y < top + 4; ++y)
        for (int x = left; x < left + 4; ++x)
          pixels.push_back(image.at<cv::Vec3b>(y, x));
  cv::Vec3b median;
  for (int channel = 0; channel < 3; ++channel) {
    std::vector<unsigned char> values;
    for (auto p : pixels)
      values.push_back(p[channel]);
    std::nth_element(values.begin(), values.begin() + values.size() / 2,
                     values.end());
    median[channel] = values[values.size() / 2];
  }
  int consistent = 0;
  for (auto p : pixels)
    consistent += cv::norm(p, median, cv::NORM_INF) <= 12;
  return consistent >= 48 ? std::optional<cv::Vec3b>(median) : std::nullopt;
}
bool indicatorColor(cv::Vec3b color) {
  const bool green = color[1] >= color[2] && color[1] >= color[0] + 15;
  const int darkest = std::min({color[0], color[1], color[2]});
  const int lightest = std::max({color[0], color[1], color[2]});
  // Premove indicators are translucent gray-purple. Exclude black/white
  // artwork and saturated marks; geometry and empty surroundings are also
  // required before any pixels can be removed.
  const bool gray =
      darkest >= 40 && lightest <= 180 && lightest - darkest <= 25;
  return green || gray;
}
// A target dot is a small green or gray-purple disk on an otherwise empty
// square. Require both its geometry and all surrounding pixels; never erase a
// region merely because its color matches an indicator.
cv::Mat removeLegalDot(const cv::Mat &square, cv::Vec3b background) {
  cv::Mat mask = cv::Mat::zeros(square.size(), CV_8U);
  for (int y = 8; y < SquarePixels - 8; ++y)
    for (int x = 8; x < SquarePixels - 8; ++x)
      if (cv::norm(square.at<cv::Vec3b>(y, x), background, cv::NORM_INF) > 18)
        mask.at<unsigned char>(y, x) = 255;
  std::vector<cv::Point> points;
  cv::findNonZero(mask, points);
  if (points.empty())
    return square;
  int left = SquarePixels, top = SquarePixels, right = 0, bottom = 0;
  for (auto point : points) {
    left = std::min(left, point.x);
    top = std::min(top, point.y);
    right = std::max(right, point.x);
    bottom = std::max(bottom, point.y);
  }
  const cv::Rect bounds(left, top, right - left + 1, bottom - top + 1);
  const double cx = bounds.x + (bounds.width - 1) / 2.0;
  const double cy = bounds.y + (bounds.height - 1) / 2.0;
  const double radius = (bounds.width + bounds.height) / 4.0;
  if (std::abs(cx - 23.5) > 7 || std::abs(cy - 23.5) > 7 || radius < 5 ||
      radius > 9 || std::abs(bounds.width - bounds.height) > 2)
    return square;
  const auto dot = square.at<cv::Vec3b>(cvRound(cy), cvRound(cx));
  if (!indicatorColor(dot) || cv::norm(dot, background, cv::NORM_INF) < 20)
    return square;
  for (int y = 8; y < SquarePixels - 8; ++y)
    for (int x = 8; x < SquarePixels - 8; ++x) {
      const double distance = std::hypot(x - cx, y - cy);
      const auto pixel = square.at<cv::Vec3b>(y, x);
      if (distance < radius - 1.5 && cv::norm(pixel, dot, cv::NORM_INF) > 18)
        return square;
      if (distance > radius + 0.5 &&
          cv::norm(pixel, background, cv::NORM_INF) > 18)
        return square;
    }
  auto cleaned = square.clone();
  cleaned(cv::Rect(4, 4, SquarePixels - 8, SquarePixels - 8)) =
      cv::Scalar(background[0], background[1], background[2]);
  return cleaned;
}
cv::Mat restoreHighlight(const cv::Mat &square, cv::Vec3b observed,
                         cv::Vec3b reference) {
  auto corrected = square.clone();
  for (int y = 0; y < corrected.rows; ++y)
    for (int x = 0; x < corrected.cols; ++x) {
      auto &pixel = corrected.at<cv::Vec3b>(y, x);
      // Reconstruct anti-aliased black/white artwork against the reference
      // background. Hard replacement leaves a colored fringe around intricate
      // pieces such as queens. Pixels inconsistent with either blend stay as
      // observed, so colored obstructions are still rejected by matching.
      double bestError = 10.0;
      cv::Vec3b replacement = pixel;
      for (double foreground : {0.0, 255.0}) {
        cv::Vec3d direction, delta;
        for (int c = 0; c < 3; ++c) {
          direction[c] = foreground - observed[c];
          delta[c] = double(pixel[c]) - observed[c];
        }
        const double denominator = direction.dot(direction);
        if (denominator < 1)
          continue;
        const double alpha =
            std::clamp(delta.dot(direction) / denominator, 0.0, 1.0);
        double error = 0;
        for (int c = 0; c < 3; ++c)
          error = std::max(error, std::abs(delta[c] - alpha * direction[c]));
        if (error <= bestError) {
          bestError = error;
          for (int c = 0; c < 3; ++c)
            replacement[c] = cv::saturate_cast<unsigned char>(
                alpha * foreground + (1 - alpha) * reference[c]);
        }
      }
      pixel = replacement;
    }
  return corrected;
}
} // namespace

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
    Entry entry{piece, dark, {}, squareBackground(image)};
    // Cache modest size variants; existing on-disk calibrations remain usable.
    for (int i = 0; i < 3; ++i) {
      const double scale = 1.0 + (i - 1) * 0.03;
      const double center = (SquarePixels - 1) / 2.0;
      const cv::Matx23d transform(scale, 0, center * (1 - scale), 0, scale,
                                  center * (1 - scale));
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
  auto background = squareBackground(square);
  // A slightly shifted board crop mixes neighboring colors at the corners.
  // Retry inside the template rim before rejecting a legal-target dot.
  if (!background)
    background = squareBackground(square, 8);
  const cv::Mat cleaned =
      background ? removeLegalDot(square, *background) : square;
  std::map<int, cv::Mat> correctedByColor;
  for (const auto &entry : templates_) {
    if (entry.dark != dark)
      continue;
    cv::Mat observed = cleaned;
    if (background && entry.background) {
      const double change =
          cv::norm(*background, *entry.background, cv::NORM_INF);
      // Green selection/legal-square and yellow last-move highlights. Do not
      // turn arbitrary solid-color occlusions into confidently empty squares.
      const bool highlight = (*background)[1] >= (*background)[0] + 15 &&
                             ((*background)[2] >= (*background)[0] + 15 ||
                              (*background)[1] >= (*background)[2] + 15);
      const auto color = *background;
      const bool premove =
          color[0] >= 80 && color[0] <= 145 && color[1] >= 70 &&
          color[1] <= 140 && color[2] >= 80 && color[2] <= 145 &&
          color[0] >= color[1] + 5 && color[2] >= color[1] + 3 &&
          std::abs(int(color[0]) - int(color[2])) <= 12;
      if ((highlight || premove) && change > 6 && change <= 110) {
        const auto color = *entry.background;
        const int key = color[0] | (color[1] << 8) | (color[2] << 16);
        auto [it, inserted] = correctedByColor.try_emplace(key);
        if (inserted)
          it->second = restoreHighlight(cleaned, *background, color);
        observed = it->second;
      }
    }
    // Compare the central artwork over a wider translation window, excluding
    // the outermost captured pixels where neighboring squares can intrude.
    // All variants compete as one class, so they cannot create false ambiguity.
    constexpr int inset = 8;
    constexpr int side = SquarePixels - 2 * inset;
    for (const auto &image : entry.images) {
      cv::Mat distance;
      cv::matchTemplate(
          observed(cv::Rect(1, 1, SquarePixels - 2, SquarePixels - 2)),
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
          cv::getRectSubPix(observed, {side, side},
                            {location.x + 1 + (side - 1) / 2.0f + dx,
                             location.y + 1 + (side - 1) / 2.0f + dy},
                            patch);
          minimum =
              std::min(minimum, cv::norm(patch, reference, cv::NORM_L2SQR));
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
  // If a central indicator-colored mark failed the empty-dot check, template
  // alignment must not dodge the remaining artwork and call it empty.
  const auto center = square.at<cv::Vec3b>(24, 24);
  const bool unverifiedMark = background && cleaned.data == square.data &&
                              indicatorColor(center) &&
                              cv::norm(center, *background, cv::NORM_INF) >= 15;
  if ((piece == '.' && unverifiedMark) || best < thresholds_.similarity ||
      best - second < thresholds_.margin)
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
