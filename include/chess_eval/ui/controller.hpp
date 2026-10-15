#pragma once
#include "chess_eval/engine.hpp"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>
#include <optional>

namespace chess_eval::ui {
enum class AnalysisInput { Manual, ConfirmedLive };
class Controller : public QObject {
  Q_OBJECT
public:
  explicit Controller(QObject *parent = nullptr);
  ~Controller() override;
  void submit(const QString &fen, EngineOptions options,
              AnalysisInput input = AnalysisInput::Manual);
  void stop();
  bool busy() const { return worker_ != nullptr; }
signals:
  void evaluated(double share, const QString &text);
  void statusChanged(const QString &text);
  void invalidated();
  void idle();

private:
  struct Request {
    Position position;
    EngineOptions options;
    quint64 generation;
    AnalysisInput input;
  };
  void dispatch();
  QTimer debounce_;
  QThread *worker_ = nullptr;
  std::shared_ptr<std::atomic_bool> cancelled_;
  std::optional<Request> pending_;
  quint64 generation_ = 0;
  QString lastKey_;
};
QString displayScore(Score score);
} // namespace chess_eval::ui
