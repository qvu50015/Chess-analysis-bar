#include "chess_eval/ui/window.hpp"
#include "chess_eval/ui/capture_panel.hpp"
#include <QCloseEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
namespace chess_eval::ui {
Window::Window() {
  setWindowTitle("Chess evaluation — Position");
  resize(620, 570);
  auto *layout = new QVBoxLayout(this);
  auto *tabs = new QTabWidget;
  layout->addWidget(tabs);
  auto *analysis = new QWidget;
  layout = new QVBoxLayout(analysis);
  tabs->addTab(analysis, "Manual analysis");
  capture_ = new CapturePanel;
  tabs->addTab(capture_, "Screen capture");
  connect(capture_, &CapturePanel::idle, this, [this] {
    if (closing_)
      close();
  });
  auto *form = new QFormLayout;
  fen_ =
      new QLineEdit("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  fen_->setObjectName("fenInput");
  fen_->setClearButtonEnabled(true);
  auto executable = QStandardPaths::findExecutable("stockfish");
  if (executable.isEmpty())
    executable = "stockfish";
  engine_ = new QLineEdit(executable);
  engine_->setObjectName("engineInput");
  depth_ = new QSpinBox;
  depth_->setRange(1, 128);
  depth_->setValue(15);
  form->addRow("&FEN", fen_);
  form->addRow("&Stockfish", engine_);
  form->addRow("&Depth", depth_);
  layout->addLayout(form);
  auto *buttons = new QHBoxLayout;
  auto *start = new QPushButton("Start");
  start->setObjectName("startButton");
  auto *stop = new QPushButton("Stop");
  auto *show = new QPushButton("Show bar");
  buttons->addWidget(start);
  buttons->addWidget(stop);
  buttons->addWidget(show);
  layout->addLayout(buttons);
  status_ = new QLabel("Enter a FEN and press Start.");
  status_->setWordWrap(true);
  status_->setMinimumHeight(44);
  layout->addWidget(status_);
  auto *hint =
      new QLabel("White grows upward · scores are from White’s perspective");
  hint->setWordWrap(true);
  layout->addWidget(hint);
  bar_ = new EvaluationBar(this);
  bar_->setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
  bar_->setWindowTitle("Evaluation");
  bar_->show();
  connect(start, &QPushButton::clicked, this, [this] {
    active_ = true;
    submit();
  });
  connect(stop, &QPushButton::clicked, this, [this] {
    active_ = false;
    controller_.stop();
  });
  connect(show, &QPushButton::clicked, bar_, [this] {
    bar_->show();
    bar_->raise();
  });
  connect(fen_, &QLineEdit::textChanged, this, [this] {
    if (active_)
      submit();
    else
      bar_->invalidate();
  });
  connect(engine_, &QLineEdit::textChanged, this, [this] {
    if (active_)
      submit();
  });
  connect(depth_, &QSpinBox::valueChanged, this, [this] {
    if (active_)
      submit();
  });
  connect(&controller_, &Controller::evaluated, bar_,
          &EvaluationBar::setEvaluation);
  connect(&controller_, &Controller::invalidated, bar_,
          &EvaluationBar::invalidate);
  connect(&controller_, &Controller::statusChanged, status_, &QLabel::setText);
  connect(&controller_, &Controller::idle, this, [this] {
    if (closing_)
      close();
  });
}
void Window::submit() {
  EngineOptions options;
  options.executable = engine_->text().trimmed().toStdString();
  options.depth = depth_->value();
  controller_.submit(fen_->text(), options);
}
void Window::closeEvent(QCloseEvent *event) {
  capture_->shutdown();
  if (controller_.busy() || capture_->busy()) {
    closing_ = true;
    active_ = false;
    controller_.stop();
    setEnabled(false);
    status_->setText("Stopping background work…");
    event->ignore();
    return;
  }
  controller_.stop();
  bar_->close();
  event->accept();
}
} // namespace chess_eval::ui
