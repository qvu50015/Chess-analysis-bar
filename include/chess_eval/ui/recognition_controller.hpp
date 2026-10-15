#pragma once
#include "chess_eval/vision/recognizer.hpp"
#include <QImage>
#include <QObject>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
namespace chess_eval::ui {
struct RecognitionRequest {
  QImage image;
  QString directory;
  vision::Orientation orientation;
  QString calibrationPlacement; // Empty means recognition; otherwise export
                                // templates.
  QString saveSnapshotPath;
};
class RecognitionController : public QObject {
  Q_OBJECT
public:
  using Processor = std::function<vision::RecognitionResult(
      const RecognitionRequest &, const std::function<bool()> &)>;
  explicit RecognitionController(QObject *parent = nullptr,
                                 Processor processor = {});
  ~RecognitionController() override;
  void submit(RecognitionRequest request);
  void invalidate();
  void shutdown();
  bool running() const { return thread_->isRunning(); }
signals:
  void completed(const chess_eval::vision::RecognitionResult &result);
  void statusChanged(const QString &message);
  void idle();

private:
  void work();
  struct Pending {
    RecognitionRequest request;
    uint64_t generation;
  };
  QThread *thread_;
  Processor processor_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::optional<Pending> pending_;
  std::atomic<uint64_t> generation_{0};
  std::atomic_bool quitting_{false};
};
} // namespace chess_eval::ui
