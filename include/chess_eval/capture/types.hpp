#pragma once
#include <QImage>
#include <QRectF>
#include <QString>
#include <cstdint>
#include <optional>

namespace chess_eval::capture {
struct Display {
  uint32_t id = 0;
  QString name;
  // Global top-left origin, logical points; may have negative x/y.
  QRectF bounds;
  double scale = 1;
  bool operator==(const Display &) const = default;
};
struct Region {
  Display display;
  QRectF rect; // Display-local logical points, never global pixels.
  bool operator==(const Region &) const = default;
};
bool validRegion(const Region &region);
QRect pixelRect(const Region &region);
std::optional<Region> regionFromGlobal(const Display &display,
                                       const QRectF &selection);
QRectF aspectFit(QSize source, const QRectF &destination);
struct Frame {
  QImage pixels; // Owned BGRA/ARGB32 pixels; immutable after publication.
  Region region;
  uint64_t session = 0;
  int64_t timestampNs =
      0; // Monotonic media presentation timestamp, not wall time.
};
bool validFrame(const Frame &frame);
enum class State { Idle, Starting, Capturing, Stopping, Error };
// GUI-thread state machine. A stop invalidates frames immediately, before
// native teardown.
class Session {
public:
  uint64_t start(const Region &region);
  bool started(uint64_t id);
  bool stop();
  bool ended(uint64_t id, bool error);
  bool accepts(const Frame &frame) const;
  State state() const { return state_; }
  uint64_t id() const { return id_; }
  bool busy() const;

private:
  State state_ = State::Idle;
  uint64_t id_ = 0;
  Region region_;
};
} // namespace chess_eval::capture
