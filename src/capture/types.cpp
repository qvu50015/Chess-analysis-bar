#include "chess_eval/capture/types.hpp"
#include <cmath>
#include <stdexcept>
namespace chess_eval::capture {
namespace {
bool finiteRect(const QRectF &r) {
  return std::isfinite(r.x()) && std::isfinite(r.y()) &&
         std::isfinite(r.width()) && std::isfinite(r.height()) &&
         r.width() > 0 && r.height() > 0;
}
} // namespace
bool validRegion(const Region &r) {
  if (!r.display.id || !finiteRect(r.display.bounds) || !finiteRect(r.rect) ||
      !std::isfinite(r.display.scale) || r.display.scale <= 0 ||
      r.display.scale > 8)
    return false;
  if (!QRectF(QPointF(0, 0), r.display.bounds.size()).contains(r.rect))
    return false;
  const double w = std::ceil(r.rect.right() * r.display.scale) -
                   std::floor(r.rect.left() * r.display.scale);
  const double h = std::ceil(r.rect.bottom() * r.display.scale) -
                   std::floor(r.rect.top() * r.display.scale);
  return w >= 1 && h >= 1 && w <= 16384 && h <= 16384 && w * h <= 64000000;
}
QRect pixelRect(const Region &r) {
  if (!validRegion(r))
    throw std::invalid_argument(
        "Invalid capture region or excessive pixel dimensions");
  const double scale = r.display.scale;
  const int x = static_cast<int>(std::floor(r.rect.x() * scale));
  const int y = static_cast<int>(std::floor(r.rect.y() * scale));
  return {x, y, static_cast<int>(std::ceil(r.rect.right() * scale)) - x,
          static_cast<int>(std::ceil(r.rect.bottom() * scale)) - y};
}
std::optional<Region> regionFromGlobal(const Display &d,
                                       const QRectF &selection) {
  Region r{d, selection.translated(-d.bounds.topLeft())};
  if (!validRegion(r))
    return std::nullopt;
  return r;
}
QRectF aspectFit(QSize source, const QRectF &destination) {
  if (source.isEmpty() || !finiteRect(destination))
    return {};
  const double scale = std::min(destination.width() / source.width(),
                                destination.height() / source.height());
  const QSizeF size(source.width() * scale, source.height() * scale);
  return {destination.center() - QPointF(size.width() / 2, size.height() / 2),
          size};
}
bool validFrame(const Frame &f) {
  return f.session != 0 && f.timestampNs >= 0 && validRegion(f.region) &&
         !f.pixels.isNull() && f.pixels.format() == QImage::Format_ARGB32 &&
         f.pixels.size() == pixelRect(f.region).size();
}
uint64_t Session::start(const Region &region) {
  if (busy() || !validRegion(region))
    throw std::invalid_argument(
        "Capture must be idle with a valid region before starting");
  region_ = region;
  ++id_;
  state_ = State::Starting;
  return id_;
}
bool Session::started(uint64_t id) {
  if (id != id_ || state_ != State::Starting)
    return false;
  state_ = State::Capturing;
  return true;
}
bool Session::stop() {
  if (state_ != State::Starting && state_ != State::Capturing)
    return false;
  state_ = State::Stopping;
  return true;
}
bool Session::ended(uint64_t id, bool error) {
  if (id != id_ || !busy())
    return false;
  state_ = error ? State::Error : State::Idle;
  return true;
}
bool Session::accepts(const Frame &f) const {
  return state_ == State::Capturing && f.session == id_ &&
         f.region == region_ && validFrame(f);
}
bool Session::busy() const {
  return state_ == State::Starting || state_ == State::Capturing ||
         state_ == State::Stopping;
}
} // namespace chess_eval::capture
