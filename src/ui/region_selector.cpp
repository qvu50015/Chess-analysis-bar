#include "chess_eval/ui/region_selector.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
namespace chess_eval::ui {
RegionSelector::RegionSelector(capture::Display display, QWidget *parent,
                               std::function<void(capture::Region)> selected)
    : QWidget(parent,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      display_(std::move(display)), selected_(std::move(selected)) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_DeleteOnClose);
  setGeometry(display_.bounds.toRect());
  setCursor(Qt::CrossCursor);
  setFocusPolicy(Qt::StrongFocus);
  setWindowTitle("Select chessboard region");
}
QPointF RegionSelector::bounded(QPointF point) const {
  return {std::clamp(point.x(), 0.0, static_cast<double>(width())),
          std::clamp(point.y(), 0.0, static_cast<double>(height()))};
}
void RegionSelector::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton)
    return;
  anchor_ = bounded(event->position());
  selection_ = {};
  dragging_ = true;
  update();
}
void RegionSelector::mouseMoveEvent(QMouseEvent *event) {
  if (dragging_) {
    selection_ = QRectF(anchor_, bounded(event->position())).normalized();
    update();
  }
}
void RegionSelector::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || !dragging_)
    return;
  selection_ = QRectF(anchor_, bounded(event->position())).normalized();
  dragging_ = false;
  update();
}
void RegionSelector::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    close();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    capture::Region region{display_, selection_};
    if (!dragging_ && capture::validRegion(region)) {
      selected_(region);
      close();
    }
  }
}
void RegionSelector::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 95));
  if (!selection_.isEmpty()) {
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(selection_, QColor(0, 0, 0, 1));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(QPen(QColor("#44d6c5"), 3));
    painter.drawRect(selection_);
  }
  const QRect banner(20, 20, std::max(100, std::min(width() - 40, 680)), 68);
  painter.fillRect(banner, QColor(20, 20, 20, 230));
  painter.setPen(Qt::white);
  painter.drawText(banner.adjusted(12, 8, -12, -8),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   QString("Drag around the board. Enter confirms · Esc "
                           "cancels.\nSelected: %1 × %2 points")
                       .arg(selection_.width(), 0, 'f', 0)
                       .arg(selection_.height(), 0, 'f', 0));
}
} // namespace chess_eval::ui
