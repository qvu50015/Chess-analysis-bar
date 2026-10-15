#pragma once
#include <QVariantAnimation>
#include <QWidget>
namespace chess_eval::ui {
class EvaluationBar : public QWidget {
public:
  explicit EvaluationBar(QWidget *parent = nullptr);
  void setEvaluation(double share, const QString &score);
  void invalidate();
  double share() const { return share_; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QVariantAnimation animation_;
  double share_ = 0.5;
  QString score_ = "—";
};
} // namespace chess_eval::ui
