#include "chess_eval/ui/recognition_controller.hpp"
#include "chess_eval/vision/qt_image.hpp"
namespace chess_eval::ui {
RecognitionController::RecognitionController(QObject *parent,
                                             Processor processor)
    : QObject(parent), processor_(std::move(processor)) {
  if (!processor_)
    processor_ = [](const RecognitionRequest &r,
                    const std::function<bool()> &cancel) {
      if (!r.saveSnapshotPath.isEmpty()) {
        if (!r.image.save(r.saveSnapshotPath))
          throw std::runtime_error("Could not save snapshot");
        return vision::RecognitionResult{};
      }
      auto bgr = vision::toBgr(r.image);
      if (cancel())
        throw std::runtime_error("Recognition cancelled");
      if (!r.calibrationPlacement.isEmpty()) {
        vision::exportTemplates(
            bgr, vision::parsePlacement(r.calibrationPlacement.toStdString()),
            r.orientation, r.directory.toStdString());
        return vision::RecognitionResult{};
      }
      vision::TemplateClassifier classifier(r.directory.toStdString());
      return vision::recognize(bgr, r.orientation, classifier, cancel);
    };
  thread_ = QThread::create([this] { work(); });
  connect(thread_, &QThread::finished, this, &RecognitionController::idle);
  thread_->start();
}
RecognitionController::~RecognitionController() {
  shutdown();
  thread_->wait();
  delete thread_;
}
void RecognitionController::invalidate() {
  ++generation_;
  std::lock_guard lock(mutex_);
  pending_.reset();
}
void RecognitionController::submit(RecognitionRequest request) {
  if (quitting_)
    return;
  const auto generation = ++generation_;
  {
    std::lock_guard lock(mutex_);
    pending_ = Pending{std::move(request), generation};
  }
  condition_.notify_one();
  emit statusChanged("Processing snapshot…");
}
void RecognitionController::shutdown() {
  quitting_ = true;
  invalidate();
  condition_.notify_one();
}
void RecognitionController::work() {
  for (;;) {
    Pending job;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock,
                      [this] { return quitting_ || pending_.has_value(); });
      if (quitting_)
        return;
      job = std::move(*pending_);
      pending_.reset();
    }
    vision::RecognitionResult result;
    QString error;
    try {
      result = processor_(job.request, [this, id = job.generation] {
        return quitting_ || generation_ != id;
      });
    } catch (const std::exception &e) {
      error = QString::fromUtf8(e.what());
    }
    QMetaObject::invokeMethod(
        this,
        [this, id = job.generation,
         calibration = !job.request.calibrationPlacement.isEmpty(),
         saving = !job.request.saveSnapshotPath.isEmpty(),
         result = std::move(result), error] {
          if (quitting_ || generation_ != id)
            return;
          if (!error.isEmpty())
            emit statusChanged("Error: " + error);
          else if (saving)
            emit statusChanged("Snapshot saved.");
          else if (calibration)
            emit statusChanged("Reference squares saved. Add examples of every "
                               "piece on both square colors.");
          else
            emit completed(result);
        },
        Qt::QueuedConnection);
  }
}
} // namespace chess_eval::ui
