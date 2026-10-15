#pragma once
#include "chess_eval/capture/backend.hpp"
#include <QObject>
#include <QTimer>
namespace chess_eval::capture {
class Manager : public QObject {
  Q_OBJECT
public:
  explicit Manager(BackendFactory factory = makeMacBackend,
                   QObject *parent = nullptr);
  ~Manager() override;
  void refreshDisplays();
  void start(const Region &region);
  void stop();
  bool busy() const { return session_.busy(); }
  State state() const { return session_.state(); }
  const std::vector<Display> &displays() const { return displays_; }
signals:
  void displaysChanged();
  void frameReady(const chess_eval::capture::Frame &frame);
  void statusChanged(const QString &status);
  void cleared();
  void idle();

private:
  void poll();
  std::shared_ptr<Mailbox> mailbox_;
  std::unique_ptr<Backend> backend_;
  Session session_;
  std::vector<Display> displays_;
  QTimer timer_;
};
} // namespace chess_eval::capture
