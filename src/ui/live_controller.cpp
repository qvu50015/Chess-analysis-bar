#include "chess_eval/ui/live_controller.hpp"
#include "chess_eval/vision/qt_image.hpp"
#include <QStringList>
#include <algorithm>
namespace chess_eval::ui {
namespace {
RecognitionController::Processor cachedProcessor() {
  struct Cache {
    uint64_t epoch = 0;
    bool flipped = false, oriented = false;
    std::unique_ptr<vision::TemplateClassifier> classifier;
    QImage image;
    vision::RecognitionResult result;
  };
  auto cache = std::make_shared<Cache>();
  // Cache belongs exclusively to the recognition thread. A new Start reloads
  // templates even when the same profile/path is selected.
  return [cache](const RecognitionRequest &request,
                 const std::function<bool()> &cancel) {
    if (!cache->classifier || cache->epoch != request.templateEpoch) {
      cache->classifier = std::make_unique<vision::TemplateClassifier>(
          request.directory.toStdString());
      if (!request.tracker || !request.tracker->synchronized()) {
        cache->flipped = false;
        cache->oriented = false;
      }
      cache->epoch = request.templateEpoch;
      cache->image = {};
    }
    if (cancel())
      throw std::runtime_error("Recognition cancelled");
    if (cache->image != request.image) {
      auto orientation = request.orientation;
      if (cache->flipped)
        orientation = orientation == vision::Orientation::WhiteBottom
                          ? vision::Orientation::BlackBottom
                          : vision::Orientation::WhiteBottom;
      auto result = vision::recognize(vision::toBgr(request.image), orientation,
                                      *cache->classifier, cancel);
      // Resolve board rotation against the known starting position. Rotation
      // changes coordinates, never the side to move (White starts either way).
      if (!cache->oriented && request.tracker && result.fenPlacement) {
        const auto expected = request.tracker->game().placement();
        if (*result.fenPlacement == expected) {
          cache->oriented = true;
        } else {
          auto rotated = result.board;
          std::reverse(rotated.squares.begin(), rotated.squares.end());
          auto placement = vision::placement(rotated);
          if (placement && *placement == expected) {
            result.board = std::move(rotated);
            result.fenPlacement = std::move(placement);
            cache->flipped = !cache->flipped;
            cache->oriented = true;
          }
        }
      }
      cache->result = std::move(result);
      cache->image = request.image;
    }
    return cache->result;
  };
}
} // namespace
LiveController::LiveController(QObject *parent,
                               RecognitionController::Processor processor)
    : QObject(parent), recognition_(nullptr, processor ? std::move(processor)
                                                       : cachedProcessor()) {
  timer_.setInterval(250);
  connect(&timer_, &QTimer::timeout, this, &LiveController::poll);
  connect(&recognition_, &RecognitionController::tracked, this,
          &LiveController::accept);
  connect(&recognition_, &RecognitionController::failed, this,
          [this](const QString &error) {
            stop("Live analysis paused: " + error +
                 " Fix the settings and Start again.");
          });
  connect(&recognition_, &RecognitionController::idle, this,
          &LiveController::idle);
}
bool LiveController::start(const QString &fen, const QString &directory,
                           vision::Orientation orientation) {
  stop();
  if (!latest_) {
    emit statusChanged("Start screen capture before live analysis.");
    return false;
  }
  try {
    tracker_.emplace(fen.toStdString());
  } catch (const std::exception &e) {
    emit statusChanged(QString::fromUtf8(e.what()));
    return false;
  }
  directory_ = directory;
  orientation_ = orientation;
  ++epoch_;
  active_ = true;
  clock_.restart();
  emit activeChanged(true);
  emit statusChanged(
      "Live analysis active — waiting for the known starting position.");
  timer_.start();
  // watch() may restore an existing tracker after start() returns. Snapshot it
  // only once that restoration is complete, rather than queueing a fresh game.
  QTimer::singleShot(0, this, &LiveController::poll);
  return true;
}
bool LiveController::watch(const QString &directory,
                           vision::Orientation orientation, bool newGame) {
  auto previous =
      !newGame && directory == directory_ && orientation == orientation_
          ? tracker_
          : std::optional<chess::Tracker>{};
  if (!start(previous ? QString::fromStdString(previous->game().fen())
                      : QString(chess::Game::InitialFen),
             directory, orientation))
    return false;
  if (previous) {
    tracker_ = std::move(previous);
    tracker_->restartObservations();
    emit statusChanged("Live analysis active — checking the tracked game.");
  }
  return true;
}
void LiveController::suspend() {
  if (!submittedFen_.isEmpty()) {
    submittedFen_.clear();
    emit suspended();
  }
}
void LiveController::stop(const QString &message) {
  active_ = false;
  timer_.stop();
  recognition_.invalidate();
  inFlight_ = false;
  submittedFen_.clear();
  emit suspended();
  emit activeChanged(false);
  emit statusChanged(message);
}
void LiveController::setFrame(const capture::Frame &frame) {
  if (!capture::validFrame(frame)) {
    clearFrame();
    return;
  }
  if (latest_ &&
      (latest_->session != frame.session || latest_->region != frame.region ||
       latest_->pixels.size() != frame.pixels.size())) {
    tracker_.reset();
    stop("Capture changed. Press Start to read the selected board again.");
  }
  if (latest_ && latest_->session == frame.session &&
      frame.timestampNs < latest_->timestampNs)
    return;
  latest_ = frame;
}
void LiveController::clearFrame() {
  latest_.reset();
  tracker_.reset();
  stop("Capture stopped or unavailable. Start capture, then Start Live "
       "Analysis.");
}
void LiveController::poll() {
  if (!active_ || !latest_ || inFlight_)
    return;
  inFlight_ = true;
  recognition_.submit({latest_->pixels,
                       directory_,
                       orientation_,
                       {},
                       {},
                       epoch_,
                       tracker_,
                       clock_.elapsed()});
}
void LiveController::accept(const vision::RecognitionResult &result,
                            const chess::Tracker &tracker,
                            const chess::Observation &observation) {
  inFlight_ = false;
  if (!active_ || !tracker_)
    return;
  tracker_ = tracker;
  using chess::TrackingState;
  if (observation.state == TrackingState::Resync) {
    suspend();
    emit statusChanged(
        "Score frozen: the board does not match the tracked "
        "game after automatic catch-up (up to four missed moves). "
        "Still watching for recovery. Reset starts tracking a new game "
        "from the standard starting board.");
  } else if (observation.fen) {
    const auto fen = QString::fromStdString(*observation.fen);
    if (submittedFen_ != fen) {
      submittedFen_ = fen;
      emit positionConfirmed(fen);
    }
    emit statusChanged("Live analysis active — position confirmed.");
  } else {
    suspend();
    if (!result.fenPlacement) {
      QStringList unknown;
      for (int i = 0; i < 64; ++i)
        if (result.board.squares[i].piece == '?')
          unknown << QString::fromStdString(vision::squareName(i));
      emit statusChanged(
          "Live analysis active — score frozen; board not readable yet. " +
          (unknown.isEmpty()
               ? QString("Checking board consistency.")
               : "Unclear squares: " + unknown.mid(0, 8).join(", ") + ".") +
          " Still watching; no restart needed.");
    } else if (!tracker_->synchronized()) {
      emit statusChanged(
          "Live analysis active — waiting for the standard starting board. "
          "Start before the first move; games already in progress cannot be "
          "initialized from pixels alone.");
    } else {
      emit statusChanged("Live analysis active — score frozen while the board "
                         "settles. Still watching for a legal position.");
    }
  }
}
void LiveController::shutdown() {
  stop();
  recognition_.shutdown();
}
} // namespace chess_eval::ui
