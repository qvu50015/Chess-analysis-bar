#pragma once
#include "chess_eval/ui/controller.hpp"
#include "chess_eval/ui/evaluation_bar.hpp"
#include <QWidget>
class QLineEdit;
class QSpinBox;
class QLabel;
namespace chess_eval::ui {
class CapturePanel;
class Window : public QWidget {
public:
  Window();

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void submit();
  Controller controller_;
  CapturePanel *capture_;
  EvaluationBar *bar_;
  QLineEdit *fen_;
  QLineEdit *engine_;
  QSpinBox *depth_;
  QLabel *status_;
  bool active_ = false;
  bool closing_ = false;
};
} // namespace chess_eval::ui
