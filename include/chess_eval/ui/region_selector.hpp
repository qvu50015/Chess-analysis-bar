#pragma once
#include "chess_eval/capture/types.hpp"
#include <QWidget>
#include <functional>
namespace chess_eval::ui {
class RegionSelector : public QWidget {
public:
  RegionSelector(capture::Display display, QWidget *parent,
                 std::function<void(capture::Region)> selected);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;

private:
  QPointF bounded(QPointF point) const;
  capture::Display display_;
  std::function<void(capture::Region)> selected_;
  QPointF anchor_;
  QRectF selection_;
  bool dragging_ = false;
};
} // namespace chess_eval::ui
