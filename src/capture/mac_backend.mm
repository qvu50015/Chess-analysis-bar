#include "chess_eval/capture/backend.hpp"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <cmath>
#include <cstring>

using namespace chess_eval::capture;
namespace {
struct NativeSession;
void stopSession(std::shared_ptr<NativeSession> session, QString error = {});
struct NativeSession {
  std::shared_ptr<Mailbox> mailbox;
  dispatch_queue_t queue;
  Region region;
  uint64_t id;
  SCStream *stream = nil;
  NSObject *output = nil;
  bool starting = false;
  bool stopping = false;
  bool stopRequested = false;
  bool ended = false;
  QString error;
};
QString message(NSError *error) {
  return error ? QString::fromUtf8(error.localizedDescription.UTF8String)
               : QString();
}
std::vector<Display> currentDisplays() {
  uint32_t count = 0;
  if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess)
    return {};
  std::vector<CGDirectDisplayID> ids(count);
  if (CGGetActiveDisplayList(count, ids.data(), &count) != kCGErrorSuccess)
    return {};
  std::vector<Display> result;
  for (uint32_t i = 0; i < count; ++i) {
    const auto id = ids[i];
    const CGRect bounds = CGDisplayBounds(id);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(id);
    const double scale =
        mode && bounds.size.width > 0
            ? static_cast<double>(CGDisplayModeGetPixelWidth(mode)) /
                  bounds.size.width
            : 1;
    if (mode)
      CGDisplayModeRelease(mode);
    result.push_back({id,
                      QString("Display %1%2")
                          .arg(id)
                          .arg(CGDisplayIsMain(id) ? " (main)" : ""),
                      {bounds.origin.x, bounds.origin.y, bounds.size.width,
                       bounds.size.height},
                      scale});
  }
  return result;
}
void finish(const std::shared_ptr<NativeSession> &s) {
  if (s->ended)
    return;
  s->ended = true;
  s->stream = nil;
  s->output = nil;
  s->mailbox->event({s->id, Event::Ended, s->error});
}
void stopSession(std::shared_ptr<NativeSession> s, QString error) {
  if (s->ended)
    return;
  s->stopRequested = true;
  if (!error.isEmpty())
    s->error = std::move(error);
  if (s->starting || s->stopping)
    return;
  if (!s->stream) {
    finish(s);
    return;
  }
  s->stopping = true;
  [s->stream stopCaptureWithCompletionHandler:^(NSError *stopError) {
    const QString text = message(stopError);
    dispatch_async(s->queue, ^{
      if (s->error.isEmpty())
        s->error = text;
      finish(s);
    });
  }];
}
} // namespace

@interface CaptureOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::weak_ptr<NativeSession> session;
}
@end
@implementation CaptureOutput
- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sample
                   ofType:(SCStreamOutputType)type {
  (void)stream;
  const auto s = session.lock();
  if (!s || s->ended || s->stopRequested || type != SCStreamOutputTypeScreen ||
      !CMSampleBufferIsValid(sample))
    return;
  auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (!attachments || !CFArrayGetCount(attachments))
    return;
  NSDictionary *details =
      (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
  NSNumber *status = details[SCStreamFrameInfoStatus];
  // Idle/incomplete frames are normal. Keep the previous valid preview.
  if (!status || status.integerValue != SCFrameStatusComplete)
    return;
  CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
  if (!buffer ||
      CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA)
    return;
  const int width = static_cast<int>(CVPixelBufferGetWidth(buffer));
  const int height = static_cast<int>(CVPixelBufferGetHeight(buffer));
  if (QSize(width, height) != pixelRect(s->region).size())
    return;
  if (CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) !=
      kCVReturnSuccess)
    return;
  const auto *base =
      static_cast<const unsigned char *>(CVPixelBufferGetBaseAddress(buffer));
  QImage image(width, height, QImage::Format_ARGB32);
  if (base && !image.isNull()) {
    const auto stride = CVPixelBufferGetBytesPerRow(buffer);
    for (int y = 0; y < height; ++y)
      std::memcpy(image.scanLine(y), base + y * stride,
                  static_cast<size_t>(width) * 4);
  }
  CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
  const CMTime timestamp = CMSampleBufferGetPresentationTimeStamp(sample);
  if (!base || image.isNull() || !CMTIME_IS_NUMERIC(timestamp))
    return;
  const auto ns =
      CMTimeConvertScale(timestamp, 1000000000, kCMTimeRoundingMethod_Default)
          .value;
  s->mailbox->publish({std::move(image), s->region, s->id, ns});
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  (void)stream;
  const auto s = session.lock();
  if (!s)
    return;
  const QString text = message(error);
  dispatch_async(s->queue, ^{
    if (s->ended)
      return;
    s->error = text.isEmpty() ? "Capture source became unavailable. Refresh "
                                "displays and select a region again."
                              : text;
    // ScreenCaptureKit has already stopped the stream on this callback.
    finish(s);
  });
}
@end

namespace chess_eval::capture {
namespace {
struct BackendState {
  dispatch_queue_t queue =
      dispatch_queue_create("chess-eval.capture", DISPATCH_QUEUE_SERIAL);
  std::shared_ptr<Mailbox> mailbox;
  std::shared_ptr<NativeSession> current;
  bool permissionRequested = false;
};
// Objective-C blocks capture reference parameters as references, not owners.
// Pass shared_ptr by value so every delayed callback retains the session.
void begin(std::shared_ptr<NativeSession> s) {
  [SCShareableContent
      getShareableContentExcludingDesktopWindows:NO
                             onScreenWindowsOnly:YES
                               completionHandler:^(SCShareableContent *content,
                                                   NSError *error) {
                                 dispatch_async(s->queue, ^{
                                   if (s->ended)
                                     return;
                                   if (s->stopRequested) {
                                     finish(s);
                                     return;
                                   }
                                   if (error) {
                                     stopSession(
                                         s,
                                         "Unable to access screen content: " +
                                             message(error) +
                                             ". Check Screen Recording "
                                             "permission in System Settings.");
                                     return;
                                   }
                                   SCDisplay *display = nil;
                                   for (SCDisplay *candidate in content
                                            .displays)
                                     if (candidate.displayID ==
                                         s->region.display.id) {
                                       display = candidate;
                                       break;
                                     }
                                   bool unchanged = false;
                                   for (const auto &candidate :
                                        currentDisplays())
                                     if (candidate == s->region.display)
                                       unchanged = true;
                                   if (!display || !unchanged) {
                                     stopSession(
                                         s, "Display disconnected or geometry "
                                            "changed. Refresh displays and "
                                            "select the region again.");
                                     return;
                                   }
                                   SCContentFilter *filter =
                                       [[SCContentFilter alloc]
                                            initWithDisplay:display
                                           excludingWindows:@[]];
                                   SCStreamConfiguration *config =
                                       [SCStreamConfiguration new];
                                   const auto rect = s->region.rect;
                                   const auto pixels = pixelRect(s->region);
                                   config.sourceRect =
                                       CGRectMake(rect.x(), rect.y(),
                                                  rect.width(), rect.height());
                                   config.width = pixels.width();
                                   config.height = pixels.height();
                                   config.minimumFrameInterval =
                                       CMTimeMake(1, 12);
                                   config.queueDepth = 3;
                                   config.pixelFormat =
                                       kCVPixelFormatType_32BGRA;
                                   config.showsCursor = NO;
                                   // M3 captures pixels only; never system
                                   // audio.
                                   if (@available(macOS 13.0, *))
                                     config.capturesAudio = NO;
                                   config.scalesToFit = YES;
                                   CaptureOutput *output = [CaptureOutput new];
                                   output->session = s;
                                   s->output = output;
                                   s->stream =
                                       [[SCStream alloc] initWithFilter:filter
                                                          configuration:config
                                                               delegate:output];
                                   NSError *addError = nil;
                                   if (![s->stream
                                              addStreamOutput:output
                                                         type:
                                                             SCStreamOutputTypeScreen
                                           sampleHandlerQueue:s->queue
                                                        error:&addError]) {
                                     s->error =
                                         "Unable to attach screen output: " +
                                         message(addError);
                                     finish(s);
                                     return;
                                   }
                                   s->starting = true;
                                   [s->stream
                                       startCaptureWithCompletionHandler:^(
                                           NSError *startError) {
                                         const QString text =
                                             message(startError);
                                         dispatch_async(s->queue, ^{
                                           if (s->ended)
                                             return;
                                           s->starting = false;
                                           if (!text.isEmpty()) {
                                             s->error =
                                                 "Capture startup failed: " +
                                                 text;
                                             finish(s);
                                           } else if (s->stopRequested)
                                             stopSession(s);
                                           else
                                             s->mailbox->event(
                                                 {s->id, Event::Started, {}});
                                         });
                                       }];
                                 });
                               }];
}
class MacBackend final : public Backend {
public:
  explicit MacBackend(std::shared_ptr<Mailbox> mailbox)
      : state_(std::make_shared<BackendState>()) {
    state_->mailbox = std::move(mailbox);
  }
  ~MacBackend() override { stop(); }
  void refreshDisplays() override {
    const auto state = state_;
    dispatch_async(state->queue, ^{
      auto displays = currentDisplays();
      const QString error =
          displays.empty() ? "No active displays are available." : QString();
      state->mailbox->displays(std::move(displays), error);
    });
  }
  void start(Region region, uint64_t id) override {
    const auto state = state_;
    dispatch_async(state->queue, ^{
      if (state->current && !state->current->ended) {
        state->mailbox->event(
            {id, Event::Ended, "Previous capture is still stopping."});
        return;
      }
      auto s = std::make_shared<NativeSession>();
      s->queue = state->queue;
      s->mailbox = state->mailbox;
      s->id = id;
      s->region = region;
      state->current = s;
      if (!validRegion(region)) {
        stopSession(s, "Invalid capture region.");
        return;
      }
      if (!CGPreflightScreenCaptureAccess()) {
        // Only an explicit Start can reach here; never prompt repeatedly.
        if (!state->permissionRequested) {
          state->permissionRequested = true;
          CGRequestScreenCaptureAccess();
        }
        if (!CGPreflightScreenCaptureAccess()) {
          stopSession(
              s,
              "Screen Recording permission is required. Open System Settings → "
              "Privacy & Security → Screen & System Audio Recording (Screen "
              "Recording on older macOS), enable Chess Evaluation Bar or the "
              "launching terminal, then relaunch the app.");
          return;
        }
      }
      begin(s);
    });
  }
  void stop() override {
    const auto state = state_;
    dispatch_async(state->queue, ^{
      if (state->current)
        stopSession(state->current);
    });
  }

private:
  std::shared_ptr<BackendState> state_;
};
} // namespace
std::unique_ptr<Backend> makeMacBackend(std::shared_ptr<Mailbox> mailbox) {
  return std::make_unique<MacBackend>(std::move(mailbox));
}
} // namespace chess_eval::capture
