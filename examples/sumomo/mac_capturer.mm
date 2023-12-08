#include "mac_capturer.h"

#include <stdint.h>
#include <chrono>
#include <functional>
#include <string>

// mac
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

// Sora C SDK
#include <sorac/types.hpp>

// libyuv
#include <libyuv.h>

NS_ASSUME_NONNULL_BEGIN

@interface SumomoMacCapturer
    : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, readonly) dispatch_queue_t frameQueue;

- (instancetype)initWithCallback:
    (std::function<void(const sorac::VideoFrame&)>)callback;

- (void)startCaptureWithDeviceName:(std::string)deviceName
                             width:(int)width
                            height:(int)height
                               fps:(NSInteger)fps
                 completionHandler:
                     (std::function<void(NSError* _Nullable)>)completionHandler;
- (void)stopCaptureWithCompletionHandler:
    (std::function<void()>)completionHandler;

@end

NS_ASSUME_NONNULL_END

const int64_t kMicrosecondsPerSecond = 1000000;
static dispatch_queue_t kCapturerQueue = nil;

@implementation SumomoMacCapturer {
  AVCaptureDevice* _device;
  AVCaptureVideoDataOutput* _videoDataOutput;
  AVCaptureSession* _captureSession;
  FourCharCode _outputPixelFormat;
  std::function<void(const sorac::VideoFrame&)> _callback;
  BOOL _willBeRunning;
  dispatch_queue_t _frameQueue;
}

- (instancetype)initWithCallback:
    (std::function<void(const sorac::VideoFrame&)>)callback {
  if (self = [super init]) {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
      kCapturerQueue = dispatch_queue_create("SumomoMacCapturerQueue",
                                             DISPATCH_QUEUE_SERIAL);
    });

    _callback = callback;
    _captureSession = [[AVCaptureSession alloc] init];
    _videoDataOutput = [[AVCaptureVideoDataOutput alloc] init];
    _willBeRunning = NO;
    _frameQueue = nil;

    NSSet<NSNumber*>* supportedPixelFormats = [NSSet
        setWithObjects:@(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
                       @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
                       @(kCVPixelFormatType_32BGRA),
                       @(kCVPixelFormatType_32ARGB), nil];
    NSMutableOrderedSet* availablePixelFormats = [NSMutableOrderedSet
        orderedSetWithArray:_videoDataOutput.availableVideoCVPixelFormatTypes];
    [availablePixelFormats intersectSet:supportedPixelFormats];
    NSNumber* pixelFormat = availablePixelFormats.firstObject;
    NSAssert(pixelFormat, @"Output device has no supported formats.");

    _outputPixelFormat = [pixelFormat unsignedIntValue];
    _videoDataOutput.videoSettings =
        @{(NSString*)kCVPixelBufferPixelFormatTypeKey : pixelFormat};
    _videoDataOutput.alwaysDiscardsLateVideoFrames = NO;
    [_videoDataOutput setSampleBufferDelegate:self queue:self.frameQueue];

    if (![_captureSession canAddOutput:_videoDataOutput]) {
      fprintf(stderr, "Video data output unsupported.");
      return nil;
    }
    [_captureSession addOutput:_videoDataOutput];

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(handleCaptureSessionRuntimeError:)
                   name:AVCaptureSessionRuntimeErrorNotification
                 object:_captureSession];
    [center addObserver:self
               selector:@selector(handleCaptureSessionDidStartRunning:)
                   name:AVCaptureSessionDidStartRunningNotification
                 object:_captureSession];
    [center addObserver:self
               selector:@selector(handleCaptureSessionDidStopRunning:)
                   name:AVCaptureSessionDidStopRunningNotification
                 object:_captureSession];
    return self;
  }
  return nil;
}

- (void)dealloc {
  NSAssert(!_willBeRunning,
           @"Session was still running in SumomoMacCapturer dealloc. Forgot to "
           @"call stopCapture?");
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (void)startCaptureWithDeviceName:(std::string)deviceName
                             width:(int)width
                            height:(int)height
                               fps:(NSInteger)fps
                 completionHandler:(std::function<void(NSError* _Nullable)>)
                                       completionHandler {
  NSArray<AVCaptureDevice*>* devices =
      [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
  if ([devices count] == 0) {
    fprintf(stderr, "No video devices\n");
    return;
  }
  if (deviceName.empty()) {
    _device = [devices objectAtIndex:0];
  } else {
    for (int i = 0; i < devices.count; i++) {
      AVCaptureDevice* device = [devices objectAtIndex:i];
      // インデックス
      if (deviceName == [@(i).stringValue UTF8String]) {
        _device = device;
        break;
      }
      // デバイス名の前方一致
      if (std::string([device.localizedName UTF8String]).find(deviceName) ==
          0) {
        _device = device;
        break;
      }
      // ユニークIDの完全一致
      if (std::string([device.uniqueID UTF8String]) == deviceName) {
        _device = device;
        break;
      }
    }
  }
  if (_device == nil) {
    fprintf(stderr, "Not found specified video device: deviceName=%s\n",
            deviceName.c_str());
    return;
  }

  // デバイスがサポートしてるフォーマットから最適なものを選択する
  AVCaptureDeviceFormat* format = nil;
  AVFrameRateRange* range = nil;
  {
    int64_t minDiff = INT64_MAX;
    int64_t value = ((int64_t)fps << 24) + width * height;
    for (AVCaptureDeviceFormat* fmt in _device.formats) {
      CMVideoDimensions dimensions =
          CMVideoFormatDescriptionGetDimensions(fmt.formatDescription);
      printf("format: %s, %dx%d\n", [fmt.description UTF8String],
             dimensions.width, dimensions.height);
      int64_t pixels = dimensions.width * dimensions.height;
      for (AVFrameRateRange* rng in fmt.videoSupportedFrameRateRanges) {
        printf("  range: %f - %f\n", rng.minFrameRate, rng.maxFrameRate);
        int64_t v = ((int64_t)rng.maxFrameRate << 24) + pixels;
        int64_t diff = llabs(value - v);
        if (diff < minDiff) {
          minDiff = diff;
          format = fmt;
          range = rng;
        }
      }
    }

    if (format == nil) {
      fprintf(
          stderr,
          "Not found specified video format: width=%d, height=%d, fps=%ld\n",
          width, height, (long)fps);
      return;
    }
  }

  _willBeRunning = YES;

  dispatch_async(kCapturerQueue, ^{
    NSError* error = nil;
    if (![_device lockForConfiguration:&error]) {
      fprintf(stderr, "Failed to lock device: error=%s\n",
              [error.localizedDescription UTF8String]);
      if (completionHandler) {
        completionHandler(error);
      }
      _willBeRunning = NO;
      return;
    }

    AVCaptureDeviceInput* input =
        [AVCaptureDeviceInput deviceInputWithDevice:_device error:&error];
    if (!input) {
      fprintf(stderr, "Failed to create front camera input: %s\n",
              [error.localizedDescription UTF8String]);
      return;
    }
    [_captureSession beginConfiguration];
    for (AVCaptureDeviceInput* oldInput in [_captureSession.inputs copy]) {
      [_captureSession removeInput:oldInput];
    }
    if ([_captureSession canAddInput:input]) {
      [_captureSession addInput:input];
    } else {
      fprintf(stderr, "Cannot add camera as an input to the session.");
    }
    [_captureSession commitConfiguration];

    _device.activeFormat = format;
    _device.activeVideoMinFrameDuration = range.minFrameDuration;
    _device.activeVideoMaxFrameDuration = range.maxFrameDuration;

    CMVideoDimensions dimensions =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    _videoDataOutput.videoSettings = @{
      (id)kCVPixelBufferWidthKey : @(dimensions.width),
      (id)kCVPixelBufferHeightKey : @(dimensions.height),
      (id)kCVPixelBufferPixelFormatTypeKey : @(_outputPixelFormat),
    };

    [_captureSession startRunning];
    [_device unlockForConfiguration];
    if (completionHandler) {
      completionHandler(nil);
    }
  });
}

- (void)stopCaptureWithCompletionHandler:
    (std::function<void()>)completionHandler {
  _willBeRunning = NO;
  dispatch_async(kCapturerQueue, ^{
    _device = nil;
    for (AVCaptureDeviceInput* oldInput in [_captureSession.inputs copy]) {
      [_captureSession removeInput:oldInput];
    }
    [_captureSession stopRunning];

    if (completionHandler) {
      completionHandler();
    }
  });
}

#pragma mark AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput*)captureOutput
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
  NSParameterAssert(captureOutput == _videoDataOutput);

  if (CMSampleBufferGetNumSamples(sampleBuffer) != 1 ||
      !CMSampleBufferIsValid(sampleBuffer) ||
      !CMSampleBufferDataIsReady(sampleBuffer)) {
    return;
  }

  CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (pixelBuffer == nil) {
    return;
  }

  int width = CVPixelBufferGetWidth(pixelBuffer);
  int height = CVPixelBufferGetHeight(pixelBuffer);
  int format = CVPixelBufferGetPixelFormatType(pixelBuffer);

  sorac::VideoFrame frame;
  frame.timestamp = std::chrono::microseconds(
      (int64_t)(CMTimeGetSeconds(
                    CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) *
                kMicrosecondsPerSecond));
  frame.nv12_buffer = sorac::VideoFrameBufferNV12::Create(width, height);
  frame.base_width = width;
  frame.base_height = height;
  uint8_t* dst_y = frame.nv12_buffer->y.get();
  int dst_stride_y = frame.nv12_buffer->stride_y;
  uint8_t* dst_uv = frame.nv12_buffer->uv.get();
  int dst_stride_uv = frame.nv12_buffer->stride_uv;

  CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

  switch (format) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: {
      const uint8_t* src_y =
          (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
      int src_stride_y = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
      const uint8_t* src_uv =
          (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
      int src_stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
      libyuv::NV12Copy(src_y, src_stride_y, src_uv, src_stride_uv, dst_y,
                       dst_stride_y, dst_uv, dst_stride_uv, width, height);
      break;
    }
    case kCVPixelFormatType_32BGRA:
    case kCVPixelFormatType_32ARGB: {
      const uint8_t* src =
          static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));
      const size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);

      if (format == kCVPixelFormatType_32BGRA) {
        libyuv::ARGBToNV12(src, stride, dst_y, dst_stride_y, dst_uv,
                           dst_stride_uv, width, height);
      } else if (format == kCVPixelFormatType_32ARGB) {
        std::unique_ptr<uint8_t[]> tmp(new uint8_t[stride * height]);
        libyuv::BGRAToARGB(src, stride, tmp.get(), stride, width, height);
        libyuv::ARGBToNV12(tmp.get(), stride, dst_y, dst_stride_y, dst_uv,
                           dst_stride_uv, width, height);
      }
      break;
    }
  }

  CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

  _callback(frame);
}

- (void)captureOutput:(AVCaptureOutput*)captureOutput
    didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection*)connection {
  fprintf(stderr, "Dropped sample buffer.\n");
}

#pragma mark - AVCaptureSession notifications

- (void)handleCaptureSessionRuntimeError:(NSNotification*)notification {
  NSError* error =
      [notification.userInfo objectForKey:AVCaptureSessionErrorKey];
  fprintf(stderr, "Capture session runtime error: %s\n",
          [error.localizedDescription UTF8String]);
}

- (void)handleCaptureSessionDidStartRunning:(NSNotification*)notification {
  printf("Capture session started.\n");
}

- (void)handleCaptureSessionDidStopRunning:(NSNotification*)notification {
  printf("Capture session stopped.\n");
}

- (dispatch_queue_t)frameQueue {
  if (!_frameQueue) {
    _frameQueue = dispatch_queue_create_with_target(
        "SumomoMacCapturerFrameQueue", DISPATCH_QUEUE_SERIAL,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
  }
  return _frameQueue;
}

@end

namespace sumomo {

class MacCapturer : public SumomoCapturer {
 public:
  MacCapturer(const char* device, int width, int height) {
    this->device_ = device;
    this->width_ = width;
    this->height_ = height;
    this->destroy = [](SumomoCapturer* p) { delete (sumomo::MacCapturer*)p; };
    this->set_frame_callback = [](SumomoCapturer* p,
                                  sumomo_capturer_on_frame_func on_frame,
                                  void* userdata) {
      ((sumomo::MacCapturer*)p)
          ->SetFrameCallback(
              [on_frame, userdata](const sorac::VideoFrame& frame) {
                sorac::VideoFrame f = frame;
                on_frame((SoracVideoFrameRef*)&f, userdata);
              });
    };
    this->start = [](SumomoCapturer* p) {
      auto q = (sumomo::MacCapturer*)p;
      return q->Start(q->device_.c_str(), q->width_, q->height_);
    };
    this->stop = [](SumomoCapturer* p) { ((sumomo::MacCapturer*)p)->Stop(); };
  }
  ~MacCapturer() { Stop(); }

  void SetFrameCallback(
      std::function<void(const sorac::VideoFrame& frame)> callback) {
    callback_ = callback;
  }

  int Start(const char* device, int width, int height) {
    Stop();

    capturer_ = [[SumomoMacCapturer alloc] initWithCallback:callback_];
    [capturer_ startCaptureWithDeviceName:device
                                    width:width
                                   height:height
                                      fps:30
                        completionHandler:[](NSError* _Nullable error) {
                          if (error) {
                            fprintf(stderr, "Failed to start capture: %s\n",
                                    [error.localizedDescription UTF8String]);
                          }
                        }];
    return 0;
  }

  void Stop() {
    [capturer_ stopCaptureWithCompletionHandler:[]() {}];
  }

 private:
  std::string device_;
  std::function<void(const sorac::VideoFrame& frame)> callback_;
  int width_;
  int height_;

  SumomoMacCapturer* capturer_;
};

}  // namespace sumomo

extern "C" {

SumomoCapturer* sumomo_mac_capturer_create(const char* device,
                                           int width,
                                           int height) {
  return new sumomo::MacCapturer(device, width, height);
}
}
