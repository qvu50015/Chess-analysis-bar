#include "chess_eval/ui/window.hpp"
#include "chess_eval/ui/capture_panel.hpp"
#include "chess_eval/ui/recognition_panel.hpp"
#include <QCloseEvent>
#include <QComboBox>
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
  recognition_ = new RecognitionPanel;
  tabs->addTab(recognition_, "Recognition");
  connect(capture_, &CapturePanel::frameAvailable, recognition_,
          &RecognitionPanel::setFrame);
  connect(capture_, &CapturePanel::frameCleared, recognition_,
          &RecognitionPanel::clearFrame);
  connect(recognition_, &RecognitionPanel::idle, this, [this] {
    if (closing_)
      close();
  });
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
  auto *liveTab = new QWidget;
  auto *liveLayout = new QVBoxLayout(liveTab);
  auto *liveHint = new QLabel(
      "Start screen capture and select calibrated templates in Recognition. "
      "Press Start Live Analysis before the first move. White or Black at the "
      "bottom is detected automatically from the starting board. Reset clears "
      "the tracked game and waits for a new standard starting board. "
      "Stockfish path and depth come from Manual analysis.");
  liveHint->setWordWrap(true);
  liveLayout->addWidget(liveHint);
  auto *liveButtons = new QHBoxLayout;
  auto *liveStart = new QPushButton("Start Live Analysis");
  liveStart->setObjectName("startLiveButton");
  auto *liveStop = new QPushButton("Stop Live Analysis");
  liveStop->setObjectName("stopLiveButton");
  liveStop->setEnabled(false);
  liveButtons->addWidget(liveStart);
  liveButtons->addWidget(liveStop);
  auto *newGame = new QPushButton("Reset");
  newGame->setObjectName("newLiveGameButton");
  liveButtons->addWidget(newGame);
  liveLayout->addLayout(liveButtons);
  auto *liveStatus = new QLabel("Live analysis stopped.");
  liveStatus->setObjectName("liveStatus");
  liveStatus->setWordWrap(true);
  liveLayout->addWidget(liveStatus);
  auto *engineStatus = new QLabel;
  engineStatus->setWordWrap(true);
  liveLayout->addWidget(engineStatus);
  liveLayout->addStretch();
  tabs->addTab(liveTab, "Live analysis");
  connect(capture_, &CapturePanel::frameAvailable, &live_,
          &LiveController::setFrame);
  connect(capture_, &CapturePanel::frameCleared, &live_,
          &LiveController::clearFrame);
  connect(recognition_, &RecognitionPanel::configurationChanged, this, [this] {
    if (live_.active())
      live_.stop("Recognition settings changed. Start live analysis again.");
  });
  auto beginLive = [this](bool reset) {
    active_ = false;
    live_.watch(recognition_->templateDirectory(), recognition_->orientation(),
                reset);
  };
  connect(liveStart, &QPushButton::clicked, this,
          [beginLive] { beginLive(false); });
  connect(newGame, &QPushButton::clicked, this,
          [beginLive] { beginLive(true); });
  connect(liveStop, &QPushButton::clicked, this, [this] { live_.stop(); });
  connect(&live_, &LiveController::activeChanged, this,
          [liveStart, liveStop](bool active) {
            liveStart->setEnabled(!active);
            liveStop->setEnabled(active);
          });
  connect(&live_, &LiveController::statusChanged, liveStatus, &QLabel::setText);
  connect(&live_, &LiveController::suspended, this, [this] {
    if (!active_)
      controller_.stop();
  });
  connect(&live_, &LiveController::positionConfirmed, this,
          [this](const QString &fen) {
            EngineOptions options;
            options.executable = engine_->text().trimmed().toStdString();
            options.depth = depth_->value();
            controller_.submit(fen, options, AnalysisInput::ConfirmedLive);
          });
  connect(&live_, &LiveController::idle, this, [this] {
    if (closing_)
      close();
  });
  connect(&controller_, &Controller::statusChanged, engineStatus,
          &QLabel::setText);
  bar_ = new EvaluationBar(this);
  bar_->setObjectName("evaluationBar");
  bar_->setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
  bar_->setWindowTitle("Evaluation");
  bar_->show();
  connect(start, &QPushButton::clicked, this, [this] {
    live_.stop();
    active_ = true;
    submit();
  });
  connect(stop, &QPushButton::clicked, this, [this] {
    active_ = false;
    live_.stop();
    controller_.stop();
  });
  connect(show, &QPushButton::clicked, bar_, [this] {
    bar_->show();
    bar_->raise();
  });
  connect(fen_, &QLineEdit::textChanged, this, [this] {
    if (active_)
      submit();
    else if (!live_.active())
      bar_->invalidate();
  });
  connect(engine_, &QLineEdit::textChanged, this, [this] {
    if (live_.active())
      live_.stop(
          "Engine settings changed. Press Start Live Analysis to resume.");
    if (active_)
      submit();
  });
  connect(depth_, &QSpinBox::valueChanged, this, [this] {
    if (live_.active())
      live_.stop(
          "Engine settings changed. Press Start Live Analysis to resume.");
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
  live_.shutdown();
  capture_->shutdown();
  recognition_->shutdown();
  if (controller_.busy() || capture_->busy() || recognition_->busy() ||
      live_.busy()) {
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
