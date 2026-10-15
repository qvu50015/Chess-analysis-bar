#pragma once
#include "chess_eval/ui/controller.hpp"
#include "chess_eval/ui/evaluation_bar.hpp"
#include "chess_eval/ui/live_controller.hpp"
#include <QWidget>
class QLineEdit;
class QSpinBox;
class QLabel;
namespace chess_eval::ui {
class CapturePanel;
class RecognitionPanel;
class Window : public QWidget {
public:
  Window();

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void submit();
  Controller controller_;
  LiveController live_;
  CapturePanel *capture_;
  RecognitionPanel *recognition_;
  EvaluationBar *bar_;
  QLineEdit *fen_;
  QLineEdit *engine_;
  QSpinBox *depth_;
  QLabel *status_;
  bool active_ = false;
  bool closing_ = false;
};
} // namespace chess_eval::ui
