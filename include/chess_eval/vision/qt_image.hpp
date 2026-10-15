#pragma once
#include <QImage>
#include <opencv2/core.hpp>
namespace chess_eval::vision {
// Owns its output. Source remains unchanged, including padded rows.
cv::Mat toBgr(const QImage &image);
} // namespace chess_eval::vision
