#pragma once
#include "chess_eval/vision/board.hpp"
#include <filesystem>
#include <functional>
#include <opencv2/core.hpp>
namespace chess_eval::vision {
constexpr int SquarePixels = 48;
bool validGeometry(int width, int height);
cv::Mat preprocess(const cv::Mat &bgr);
std::array<cv::Mat, 64> extractSquares(const cv::Mat &normalized);
class PieceClassifier {
public:
  virtual ~PieceClassifier() = default;
  virtual Square classify(const cv::Mat &square, bool dark) const = 0;
};
struct Thresholds {
  double similarity = 0.92;
  double margin = 0.025;
};
class TemplateClassifier : public PieceClassifier {
public:
  explicit TemplateClassifier(const std::filesystem::path &directory,
                              Thresholds thresholds = {});
  Square classify(const cv::Mat &square, bool dark) const override;

private:
  struct Entry {
    char piece;
    bool dark;
    std::array<cv::Mat, 3> images;
    std::optional<cv::Vec3b> background;
  };
  std::vector<Entry> templates_;
  Thresholds thresholds_;
};
// Worker-local cache. Reset when templates or classifier settings change.
// Entries are in screen order, so orientation changes only remap coordinates.
struct RecognitionCache {
  cv::Mat normalized;
  std::array<Square, 64> squares{};
  const PieceClassifier *classifier = nullptr;
};
RecognitionResult recognize(const cv::Mat &bgr, Orientation orientation,
                            const PieceClassifier &classifier,
                            const std::function<bool()> &cancel = {},
                            RecognitionCache *cache = nullptr);
// Extract labeled squares from a permitted board image. Repeated calls add
// variants.
void exportTemplates(const cv::Mat &bgr, const Board &labels,
                     Orientation orientation,
                     const std::filesystem::path &directory);
} // namespace chess_eval::vision
