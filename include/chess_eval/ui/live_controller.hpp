#pragma once
#include "chess_eval/capture/types.hpp"
#include "chess_eval/chess/tracker.hpp"
#include "chess_eval/ui/recognition_controller.hpp"
#include <QElapsedTimer>
#include <QTimer>

namespace chess_eval::ui {
// GUI-thread coordinator. Recognition owns a persistent worker and consumes
// only the latest frame; engine ownership remains with the existing UI
// Controller.
class LiveController : public QObject {
  Q_OBJECT
public:
  explicit LiveController(QObject *parent = nullptr,
                          RecognitionController::Processor processor = {});
  bool start(const QString &fen, const QString &directory,
             vision::Orientation orientation);
  bool watch(const QString &directory, vision::Orientation orientation,
             bool newGame = false);
  void stop(const QString &message = "Live analysis stopped.");
  void setFrame(const capture::Frame &frame);
  void clearFrame();
  void shutdown();
  bool active() const { return active_; }
  bool busy() const { return recognition_.running(); }
signals:
  void positionConfirmed(const QString &fen);
  void suspended();
  void statusChanged(const QString &message);
  void activeChanged(bool active);
  void idle();

private:
  void poll();
  void accept(const vision::RecognitionResult &result,
              const chess::Tracker &tracker,
              const chess::Observation &observation);
  void suspend();
  RecognitionController recognition_;
  QTimer timer_;
  QElapsedTimer clock_;
  std::optional<capture::Frame> latest_;
  std::optional<chess::Tracker> tracker_;
  QString directory_, submittedFen_;
  QImage submittedImage_;
  vision::Orientation orientation_ = vision::Orientation::WhiteBottom;
  uint64_t epoch_ = 0;
  bool active_ = false, inFlight_ = false;
};
} // namespace chess_eval::ui
