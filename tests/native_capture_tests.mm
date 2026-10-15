// Include the implementation to exercise its private native callback boundary.
// ScreenCaptureKit enumeration is replaced only inside this test process.
#include "../src/capture/mac_backend.mm"
#include <iostream>
#import <objc/runtime.h>
#include <stdexcept>

@interface DeferredStopStream : NSObject
@property(nonatomic, copy) void (^completion)(NSError *);
- (void)stopCaptureWithCompletionHandler:(void (^)(NSError *))handler;
@end
@implementation DeferredStopStream
- (void)stopCaptureWithCompletionHandler:(void (^)(NSError *))handler {
  self.completion = handler;
}
@end

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  @autoreleasepool {
    try {
      auto mailbox = std::make_shared<Mailbox>();
      auto queue =
          dispatch_queue_create("capture-lifetime-test", DISPATCH_QUEUE_SERIAL);
      __block void (^enumeration)(SCShareableContent *, NSError *) = nil;
      Method method = class_getClassMethod(
          [SCShareableContent class], @selector
          (getShareableContentExcludingDesktopWindows:
                                  onScreenWindowsOnly:completionHandler:));
      IMP mock = imp_implementationWithBlock(^(
          id, BOOL, BOOL, void (^completion)(SCShareableContent *, NSError *)) {
        enumeration = [completion copy];
      });
      IMP original = method_setImplementation(method, mock);
      std::weak_ptr<NativeSession> weak;
      {
        auto session = std::make_shared<NativeSession>();
        session->mailbox = mailbox;
        session->queue = queue;
        session->id = 17;
        weak = session;
        begin(session);
      }
      const bool retained = !weak.expired();
      // Restore the framework entry point before checking results.
      method_setImplementation(method, original);
      imp_removeBlock(mock);
      check(retained,
            "Enumeration callback must own the session after begin returns");
      enumeration(nil, [NSError errorWithDomain:@"capture-test"
                                           code:1
                                       userInfo:@{
                                         NSLocalizedDescriptionKey :
                                             @"Mock permission failure"
                                       }]);
      enumeration = nil;
      dispatch_sync(queue, ^{
                    });
      auto result = mailbox->take();
      check(result.events.size() == 1 && result.events[0].session == 17 &&
                result.events[0].kind == Event::Ended &&
                result.events[0].error.contains("Mock permission failure"),
            "Deferred enumeration error delivered safely");
      check(weak.expired(), "Enumeration callback releases its session");

      DeferredStopStream *stream = [DeferredStopStream new];
      {
        auto session = std::make_shared<NativeSession>();
        session->mailbox = mailbox;
        session->queue = queue;
        session->id = 18;
        session->stream = (SCStream *)stream;
        weak = session;
        stopSession(session);
      }
      check(!weak.expired(),
            "Stop callback must own the session after stopSession returns");
      stream.completion(nil);
      stream.completion = nil;
      dispatch_sync(queue, ^{
                    });
      result = mailbox->take();
      check(result.events.size() == 1 && result.events[0].session == 18 &&
                result.events[0].error.isEmpty(),
            "Deferred stop completion delivered safely");
      check(weak.expired(), "Stop callback releases its session");
      std::cout << "Native delayed enumeration and stop callback lifetime "
                   "tests passed (no screen access)\n";
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  return 0;
}
