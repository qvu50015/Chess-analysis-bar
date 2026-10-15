#include "chess_eval/ui/capture_panel.hpp"
#include "chess_eval/ui/region_selector.hpp"
#include <QComboBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace chess_eval::ui {
CapturePreview::CapturePreview(QWidget *parent) : QWidget(parent) {
  setMinimumSize(240, 180);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void CapturePreview::setFrame(const capture::Frame &frame) {
  image_ = frame.pixels;
  placeholder_.clear();
  update();
}
void CapturePreview::clear(const QString &placeholder) {
  image_ = {};
  placeholder_ = placeholder;
  update();
}
void CapturePreview::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#202428"));
  if (!image_.isNull()) {
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(capture::aspectFit(image_.size(),
                                         QRectF(rect()).adjusted(6, 6, -6, -6)),
                      image_);
  } else {
    painter.setPen(QColor("#e0e5e8"));
    painter.drawText(rect().adjusted(16, 16, -16, -16),
                     Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
  }
}
CapturePanel::CapturePanel(QWidget *parent, capture::BackendFactory factory)
    : QWidget(parent), manager_(std::move(factory)) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *row = new QHBoxLayout;
  displays_ = new QComboBox;
  displays_->setAccessibleName("Capture display");
  auto *refresh = new QPushButton("Refresh displays");
  row->addWidget(displays_, 1);
  row->addWidget(refresh);
  layout->addLayout(row);
  auto *buttons = new QHBoxLayout;
  select_ = new QPushButton("Select Region");
  start_ = new QPushButton("Start Capture");
  stop_ = new QPushButton("Stop Capture");
  buttons->addWidget(select_);
  buttons->addWidget(start_);
  buttons->addWidget(stop_);
  layout->addLayout(buttons);
  geometry_ = new QLabel("No region selected");
  geometry_->setWordWrap(true);
  layout->addWidget(geometry_);
  preview_ = new CapturePreview;
  layout->addWidget(preview_, 1);
  status_ = new QLabel("Idle");
  status_->setWordWrap(true);
  layout->addWidget(status_);
  connect(refresh, &QPushButton::clicked, &manager_,
          &capture::Manager::refreshDisplays);
  connect(select_, &QPushButton::clicked, this, &CapturePanel::selectRegion);
  connect(start_, &QPushButton::clicked, this, [this] {
    if (region_)
      manager_.start(*region_);
    updateButtons();
  });
  connect(stop_, &QPushButton::clicked, this, [this] {
    manager_.stop();
    updateButtons();
  });
  connect(displays_, &QComboBox::currentIndexChanged, this, [this] {
    manager_.stop();
    region_.reset();
    emit frameCleared();
    geometry_->setText("No region selected");
    preview_->clear("Select a region on this display");
    updateButtons();
  });
  connect(&manager_, &capture::Manager::displaysChanged, this,
          &CapturePanel::refreshList);
  connect(&manager_, &capture::Manager::frameReady, this,
          &CapturePanel::frameAvailable);
  connect(&manager_, &capture::Manager::cleared, this,
          &CapturePanel::frameCleared);
  connect(&manager_, &capture::Manager::frameReady, preview_,
          &CapturePreview::setFrame);
  connect(&manager_, &capture::Manager::cleared, this,
          [this] { preview_->clear(); });
  connect(&manager_, &capture::Manager::statusChanged, this,
          [this](const QString &text) {
            status_->setText(text);
            updateButtons();
          });
  connect(&manager_, &capture::Manager::idle, this, [this] {
    updateButtons();
    emit idle();
  });
  connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
    watchScreens();
    manager_.refreshDisplays();
  });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
    manager_.stop();
    manager_.refreshDisplays();
  });
  watchScreens();
  manager_.refreshDisplays();
  updateButtons();
}
void CapturePanel::watchScreens() {
  for (auto *screen : QGuiApplication::screens()) {
    disconnect(screen, nullptr, this, nullptr);
    connect(screen, &QScreen::geometryChanged, this, [this] {
      manager_.stop();
      manager_.refreshDisplays();
    });
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] {
      manager_.stop();
      manager_.refreshDisplays();
    });
  }
}
void CapturePanel::refreshList() {
  const auto oldId = displays_->currentData().toUInt();
  const QSignalBlocker blocker(displays_);
  displays_->clear();
  bool valid = !region_;
  for (const auto &display : manager_.displays()) {
    displays_->addItem(QString("%1 · %2 × %3 points · %4×")
                           .arg(display.name)
                           .arg(display.bounds.width())
                           .arg(display.bounds.height())
                           .arg(display.scale),
                       display.id);
    if (region_ && display == region_->display)
      valid = true;
  }
  const int index = displays_->findData(oldId);
  if (index >= 0)
    displays_->setCurrentIndex(index);
  if (!valid) {
    manager_.stop();
    region_.reset();
    emit frameCleared();
    if (selector_)
      selector_->close();
    geometry_->setText("Display changed. Select the region again.");
    preview_->clear();
  }
  updateButtons();
}
void CapturePanel::selectRegion() {
  if (manager_.busy() || selector_)
    return;
  const auto id = displays_->currentData().toUInt();
  for (const auto &display : manager_.displays()) {
    if (display.id != id)
      continue;
    // Both APIs use top-left logical display coordinates. Fail closed if the
    // desktop geometry changed between enumeration and opening the overlay.
    bool matched = false;
    for (auto *screen : QGuiApplication::screens())
      if (screen->geometry() == display.bounds.toRect())
        matched = true;
    if (!matched) {
      status_->setText(
          "Error: display geometry changed. Refresh displays and retry.");
      return;
    }
    auto *selector =
        new RegionSelector(display, this, [this](capture::Region region) {
          region_ = region;
          emit frameCleared();
          const auto pixels = capture::pixelRect(region);
          geometry_->setText(
              QString(
                  "Selected on %1: (%2, %3), %4 × %5 points → %6 × %7 pixels")
                  .arg(region.display.name)
                  .arg(region.rect.x())
                  .arg(region.rect.y())
                  .arg(region.rect.width())
                  .arg(region.rect.height())
                  .arg(pixels.width())
                  .arg(pixels.height()));
          preview_->clear("Region selected. Press Start Capture to preview.");
          updateButtons();
        });
    selector_ = selector;
    connect(selector, &QObject::destroyed, this, [this] {
      // Explicitly release the selection guard before recomputing controls;
      // do not depend on QPointer invalidation ordering during destruction.
      selector_.clear();
      updateButtons();
    });
    selector->show();
    selector->raise();
    selector->activateWindow();
    selector->setFocus();
    updateButtons();
    return;
  }
}
void CapturePanel::updateButtons() {
  select_->setEnabled(!closing_ && !manager_.busy() && !selector_ &&
                      displays_->count() > 0);
  start_->setEnabled(!closing_ && !manager_.busy() && !selector_ &&
                     region_.has_value());
  stop_->setEnabled(!closing_ && manager_.busy() &&
                    manager_.state() != capture::State::Stopping);
}
void CapturePanel::shutdown() {
  closing_ = true;
  if (selector_)
    selector_->close();
  manager_.stop();
  updateButtons();
}
} // namespace chess_eval::ui
