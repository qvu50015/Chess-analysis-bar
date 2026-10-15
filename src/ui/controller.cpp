#include "chess_eval/ui/controller.hpp"

namespace chess_eval::ui {
QString displayScore(Score score) {
  if (score.kind == ScoreKind::mate) {
    const bool negative =
        score.value < 0 || (score.value == 0 && score.negative_zero);
    return QString(negative ? "−M%1" : "+M%1")
        .arg(std::abs(static_cast<long long>(score.value)));
  }
  return QString(score.value < 0 ? "" : "+") +
         QString::number(score.value / 100.0, 'f', 2);
}
Controller::Controller(QObject *parent) : QObject(parent) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(350);
  connect(&debounce_, &QTimer::timeout, this, &Controller::dispatch);
}
Controller::~Controller() {
  if (cancelled_)
    cancelled_->store(true);
  // Normal window close waits asynchronously for idle before destruction.
  if (worker_) {
    worker_->wait();
    delete worker_;
  }
}
void Controller::stop() {
  ++generation_;
  debounce_.stop();
  pending_.reset();
  lastKey_.clear();
  if (cancelled_)
    cancelled_->store(true);
  emit invalidated();
  emit statusChanged("Stopped");
}
void Controller::submit(const QString &fen, EngineOptions options,
                        AnalysisInput input) {
  Position position;
  try {
    position = parse_fen(fen.toStdString());
  } catch (const std::exception &error) {
    stop();
    emit statusChanged(QString::fromUtf8(error.what()));
    return;
  }
  const auto key = QString::fromStdString(position.fen) + '\n' +
                   QString::fromStdString(options.executable) + '\n' +
                   QString::number(options.depth) + '\n' +
                   QString::number(options.timeout.count()) + '\n' +
                   QString::number(static_cast<int>(input));
  if (key == lastKey_)
    return;
  stop();
  lastKey_ = key;
  pending_ =
      Request{std::move(position), std::move(options), generation_, input};
  if (input == AnalysisInput::ConfirmedLive) {
    // The tracker has already confirmed stability; do not debounce it again.
    dispatch();
  } else {
    emit statusChanged("Waiting for input…");
    debounce_.start();
  }
}
void Controller::dispatch() {
  if (worker_ || !pending_)
    return;
  const auto request = std::move(*pending_);
  pending_.reset();
  cancelled_ = std::make_shared<std::atomic_bool>(false);
  const auto cancel = cancelled_;
  emit statusChanged("Analyzing…");
  worker_ = QThread::create([this, request, cancel] {
    std::optional<Score> score;
    QString error;
    try {
      auto lastProgress = std::chrono::steady_clock::time_point{};
      score = analyze(
          request.position, request.options,
          [cancel] { return cancel->load(); },
          [this, &request, &lastProgress, cancel](Score value, int depth) {
            if (request.input != AnalysisInput::ConfirmedLive ||
                cancel->load() || (depth < 8 && value.kind != ScoreKind::mate))
              return;
            const auto now = std::chrono::steady_clock::now();
            if (now - lastProgress < std::chrono::milliseconds(100))
              return;
            lastProgress = now;
            QMetaObject::invokeMethod(
                this,
                [this, id = request.generation, value] {
                  if (id == generation_)
                    emit evaluated(white_share(value), displayScore(value));
                },
                Qt::QueuedConnection);
          });
    } catch (const std::exception &exception) {
      error = QString::fromUtf8(exception.what());
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation = request.generation, score, error] {
          // Only the GUI thread reads generation_ or touches widgets.
          if (generation != generation_)
            return;
          if (score) {
            emit evaluated(white_share(*score), displayScore(*score));
            emit statusChanged("Analysis complete");
          } else {
            lastKey_.clear();
            emit invalidated();
            emit statusChanged(error);
          }
        },
        Qt::QueuedConnection);
  });
  connect(worker_, &QThread::finished, this, [this] {
    worker_->deleteLater();
    worker_ = nullptr;
    if (pending_ && !debounce_.isActive())
      dispatch();
    if (!worker_)
      emit idle();
  });
  worker_->start();
}
} // namespace chess_eval::ui
