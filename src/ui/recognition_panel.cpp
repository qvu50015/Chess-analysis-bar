#include "chess_eval/ui/recognition_panel.hpp"
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
namespace chess_eval::ui {
RecognitionPanel::RecognitionPanel(QWidget *parent) : QWidget(parent) {
  setMinimumWidth(480);
  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  profile_ = new QComboBox;
  profile_->setObjectName("recognitionProfile");
  profile_->addItem("Chess.com · Green / Classic", "chesscom-green-classic");
  profile_->addItem("Lichess · Brown / Cburnett", "lichess-brown-cburnett");
  profile_->addItem("Custom profile", "custom");
  orientation_ = new QComboBox;
  orientation_->addItems({"White at bottom", "Black at bottom"});
  directory_ = new QLineEdit;
  directory_->setObjectName("templateDirectory");
  auto *browse = new QPushButton("Browse…");
  auto *directoryRow = new QHBoxLayout;
  directoryRow->addWidget(directory_);
  directoryRow->addWidget(browse);
  form->addRow("Profile", profile_);
  form->addRow("Orientation", orientation_);
  form->addRow("Templates", directoryRow);
  layout->addLayout(form);
  auto profileChanged = [this] {
    profileDirectories_[previousProfile_] = directory_->text();
    previousProfile_ = profile_->currentIndex();
    auto &path = profileDirectories_[previousProfile_];
    if (path.isEmpty())
      path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
             "/templates/" + profile_->currentData().toString();
    directory_->setText(path);
    invalidate();
  };
  connect(profile_, &QComboBox::currentIndexChanged, this, profileChanged);
  connect(orientation_, &QComboBox::currentIndexChanged, this,
          [this] { invalidate(); });
  connect(directory_, &QLineEdit::textChanged, this, [this] { invalidate(); });
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(
        this, "Template directory", directory_->text());
    if (!path.isEmpty())
      directory_->setText(path);
  });
  auto *recognize = new QPushButton("Recognize Position");
  recognize->setObjectName("recognizeButton");
  layout->addWidget(recognize);
  connect(recognize, &QPushButton::clicked, this, [this] { request(false); });
  source_ =
      new QLabel("No captured frame. Start capture in the Screen capture tab.");
  source_->setWordWrap(true);
  layout->addWidget(source_);
  board_ = new QTableWidget(8, 8);
  board_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  board_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  board_->verticalHeader()->setMinimumSectionSize(16);
  board_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  board_->setHorizontalHeaderLabels({"a", "b", "c", "d", "e", "f", "g", "h"});
  board_->setVerticalHeaderLabels({"8", "7", "6", "5", "4", "3", "2", "1"});
  board_->setMinimumHeight(240);
  layout->addWidget(board_, 1);
  placement_ = new QLineEdit;
  placement_->setObjectName("recognizedPlacement");
  placement_->setReadOnly(true);
  placement_->setPlaceholderText(
      "Piece placement only — no side-to-move or rights inferred");
  copy_ = new QPushButton("Copy placement");
  copy_->setEnabled(false);
  auto *output = new QHBoxLayout;
  output->addWidget(placement_);
  output->addWidget(copy_);
  layout->addLayout(output);
  connect(copy_, &QPushButton::clicked, this, [this] {
    QGuiApplication::clipboard()->setText(placement_->text());
  });
  labels_ = new QLineEdit;
  labels_->setPlaceholderText("Calibration: enter the exact piece-placement "
                              "labels for the current board");
  layout->addWidget(labels_);
  auto *calibrate =
      new QPushButton("Save labeled reference squares to this profile");
  layout->addWidget(calibrate);
  connect(calibrate, &QPushButton::clicked, this, [this] { request(true); });
  auto *save = new QPushButton("Save analyzed snapshot…");
  layout->addWidget(save);
  connect(save, &QPushButton::clicked, this, [this] {
    if (snapshot_.isNull()) {
      status_->setText("Recognize a captured snapshot first.");
      return;
    }
    const auto file = QFileDialog::getSaveFileName(
        this, "Save reference snapshot", {}, "PNG (*.png)");
    if (!file.isEmpty())
      controller_.submit(
          {snapshot_, {}, vision::Orientation::WhiteBottom, {}, file});
  });
  status_ = new QLabel("Templates must be calibrated for the selected profile "
                       "before recognition.");
  status_->setWordWrap(true);
  layout->addWidget(status_);
  connect(&controller_, &RecognitionController::statusChanged, status_,
          &QLabel::setText);
  connect(&controller_, &RecognitionController::completed, this,
          &RecognitionPanel::showResult);
  connect(&controller_, &RecognitionController::idle, this,
          &RecognitionPanel::idle);
  profileChanged();
}
QString RecognitionPanel::templateDirectory() const {
  return directory_->text();
}
vision::Orientation RecognitionPanel::orientation() const {
  return orientation_->currentIndex() == 0 ? vision::Orientation::WhiteBottom
                                           : vision::Orientation::BlackBottom;
}
void RecognitionPanel::invalidate() {
  emit configurationChanged();
  controller_.invalidate();
  if (placement_)
    placement_->clear();
  if (copy_)
    copy_->setEnabled(false);
  if (board_)
    board_->clearContents();
  if (status_)
    status_->setText("Settings changed. Recognize again.");
}
void RecognitionPanel::setFrame(const capture::Frame &frame) {
  if (latest_ &&
      (latest_->session != frame.session || latest_->region != frame.region))
    invalidate();
  latest_ = frame;
  source_->setText(QString("Latest capture: %1 × %2 pixels. Displayed results "
                           "describe the explicitly requested snapshot.")
                       .arg(frame.pixels.width())
                       .arg(frame.pixels.height()));
}
void RecognitionPanel::clearFrame() {
  latest_.reset();
  snapshot_ = {};
  invalidate();
  source_->setText(
      "No captured frame. Start capture in the Screen capture tab.");
}
void RecognitionPanel::request(bool calibration) {
  invalidate();
  if (!latest_) {
    status_->setText("No captured frame available. Start capture first.");
    return;
  }
  if (calibration && labels_->text().isEmpty()) {
    status_->setText(
        "Enter accurate piece-placement labels before calibration.");
    return;
  }
  snapshot_ = latest_->pixels;
  controller_.submit({snapshot_,
                      directory_->text(),
                      orientation_->currentIndex() == 0
                          ? vision::Orientation::WhiteBottom
                          : vision::Orientation::BlackBottom,
                      calibration ? labels_->text() : QString(),
                      {}});
}
void RecognitionPanel::showResult(const vision::RecognitionResult &result) {
  double minimum = 1;
  for (int i = 0; i < 64; ++i) {
    const auto square = result.board.squares[i];
    minimum = std::min(minimum, square.confidence);
    auto *item = new QTableWidgetItem(QString(QChar(square.piece)));
    item->setTextAlignment(Qt::AlignCenter);
    item->setToolTip(QString("%1 · match similarity %2%")
                         .arg(QString::fromStdString(vision::squareName(i)))
                         .arg(square.confidence * 100, 0, 'f', 1));
    item->setBackground(square.piece == '?'   ? QColor("#e5b75c")
                        : (i / 8 + i % 8) % 2 ? QColor("#b5b5b5")
                                              : QColor("#eeeeee"));
    item->setForeground(Qt::black);
    board_->setItem(i / 8, i % 8, item);
  }
  QStringList errors;
  for (const auto &error : result.errors)
    errors << QString::fromStdString(error);
  placement_->setText(result.fenPlacement
                          ? QString::fromStdString(*result.fenPlacement)
                          : QString());
  copy_->setEnabled(result.fenPlacement.has_value());
  status_->setText(QString("%1 ms · minimum similarity %2%. %3")
                       .arg(result.milliseconds, 0, 'f', 1)
                       .arg(minimum * 100, 0, 'f', 1)
                       .arg(errors.isEmpty()
                                ? "Structurally valid placement; full chess "
                                  "legality is not verified."
                                : errors.join("; ")));
}
void RecognitionPanel::shutdown() { controller_.shutdown(); }
} // namespace chess_eval::ui
