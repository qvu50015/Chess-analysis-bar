#include "chess_eval/vision/qt_image.hpp"
#include <opencv2/imgproc.hpp>
#include <stdexcept>
namespace chess_eval::vision {
cv::Mat toBgr(const QImage &image) {
  if (image.isNull())
    throw std::invalid_argument("No captured image available");
    // Normalize uncommon and premultiplied formats before exposing row stride.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
  if (image.format() == QImage::Format_ARGB32 ||
      image.format() == QImage::Format_RGB32) {
    const cv::Mat view(image.height(), image.width(), CV_8UC4,
                       const_cast<uchar *>(image.constBits()),
                       static_cast<size_t>(image.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
  }
#endif
  const auto rgb = image.format() == QImage::Format_RGB888
                       ? image
                       : image.convertToFormat(QImage::Format_RGB888);
  const cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
                     const_cast<uchar *>(rgb.constBits()),
                     static_cast<size_t>(rgb.bytesPerLine()));
  cv::Mat bgr;
  cv::cvtColor(view, bgr, cv::COLOR_RGB2BGR);
  return bgr;
}
} // namespace chess_eval::vision
