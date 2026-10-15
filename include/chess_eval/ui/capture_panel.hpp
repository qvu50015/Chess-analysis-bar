#pragma once
#include "chess_eval/capture/manager.hpp"
#include <QPointer>
#include <QWidget>
class QComboBox;
class QPushButton;
class QLabel;
namespace chess_eval::ui {
class CapturePreview : public QWidget {
public:
  explicit CapturePreview(QWidget *parent = nullptr);
  void setFrame(const capture::Frame &frame);
  void clear(const QString &placeholder = "Capture inactive");

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QImage image_;
  QString placeholder_ = "Select a display and a region to preview";
};
class CapturePanel : public QWidget {
  Q_OBJECT
public:
  explicit CapturePanel(
      QWidget *parent = nullptr,
      capture::BackendFactory factory = capture::makeMacBackend);
  bool busy() const { return manager_.busy(); }
  void shutdown();
signals:
  void idle();

private:
  void refreshList();
  void selectRegion();
  void updateButtons();
  void watchScreens();
  capture::Manager manager_;
  QComboBox *displays_;
  QPushButton *select_;
  QPushButton *start_;
  QPushButton *stop_;
  QLabel *geometry_;
  QLabel *status_;
  CapturePreview *preview_;
  QPointer<QWidget> selector_;
  std::optional<capture::Region> region_;
  bool closing_ = false;
};
} // namespace chess_eval::ui
