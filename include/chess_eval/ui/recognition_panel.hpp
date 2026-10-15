#pragma once
#include "chess_eval/capture/types.hpp"
#include "chess_eval/ui/recognition_controller.hpp"
#include <QWidget>
class QComboBox;
class QLineEdit;
class QLabel;
class QTableWidget;
class QPushButton;
namespace chess_eval::ui {
class RecognitionPanel : public QWidget {
  Q_OBJECT
public:
  explicit RecognitionPanel(QWidget *parent = nullptr);
  void setFrame(const capture::Frame &frame);
  void clearFrame();
  void shutdown();
  bool busy() const { return controller_.running(); }
signals:
  void idle();

private:
  void request(bool calibration);
  void invalidate();
  void showResult(const vision::RecognitionResult &result);
  RecognitionController controller_;
  std::optional<capture::Frame> latest_;
  QImage snapshot_;
  std::array<QString, 3> profileDirectories_;
  int previousProfile_ = 0;
  QComboBox *profile_ = nullptr;
  QComboBox *orientation_ = nullptr;
  QLineEdit *directory_ = nullptr;
  QLineEdit *labels_ = nullptr;
  QLineEdit *placement_ = nullptr;
  QTableWidget *board_ = nullptr;
  QLabel *status_ = nullptr;
  QLabel *source_ = nullptr;
  QPushButton *copy_ = nullptr;
};
} // namespace chess_eval::ui
