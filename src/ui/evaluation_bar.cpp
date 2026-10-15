#include "chess_eval/ui/evaluation_bar.hpp"
#include <QPainter>
#include <algorithm>
namespace chess_eval::ui {
EvaluationBar::EvaluationBar(QWidget *parent) : QWidget(parent) {
  setMinimumSize(64, 180);
  resize(88, 420);
  setAccessibleName("White perspective evaluation bar");
  animation_.setDuration(320);
  animation_.setEasingCurve(QEasingCurve::InOutCubic);
  connect(&animation_, &QVariantAnimation::valueChanged, this,
          [this](const QVariant &value) {
            share_ = value.toDouble();
            update();
          });
}
void EvaluationBar::setEvaluation(double share, const QString &score) {
  score_ = score;
  setAccessibleDescription("White evaluation " + score);
  animation_.stop();
  animation_.setStartValue(share_);
  animation_.setEndValue(std::clamp(share, 0.0, 1.0));
  animation_.start();
  update();
}
void EvaluationBar::invalidate() {
  animation_.stop();
  score_ = "—";
  setAccessibleDescription("No current evaluation");
  update();
}
void EvaluationBar::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#161616"));
  const double split = height() * (1.0 - share_);
  painter.fillRect(QRectF(0, split, width(), height() - split), Qt::white);
  painter.setPen(QColor("#808080"));
  painter.drawLine(0, height() / 2, width(), height() / 2);
  // Fixed contrasting badge stays readable even while the boundary moves.
  const QRect badge(4, height() / 2 - 17, width() - 8, 34);
  painter.fillRect(badge, QColor("#161616"));
  painter.setPen(Qt::white);
  auto font = painter.font();
  font.setBold(true);
  font.setPointSize(12);
  painter.setFont(font);
  painter.drawText(badge, Qt::AlignCenter, score_);
}
} // namespace chess_eval::ui
