#ifndef SORAC_DATA_CHANNEL_HPP_
#define SORAC_DATA_CHANNEL_HPP_

#include <functional>
#include <memory>

// libdatachannel
#include <rtc/rtc.hpp>

namespace sorac {

// 圧縮対応した DataChannel
class DataChannel {
 public:
  virtual ~DataChannel() {}

  virtual std::string GetLabel() const = 0;
  virtual bool Send(const uint8_t* buf, size_t size) = 0;

  virtual void SetOnOpen(std::function<void()> on_open) = 0;
  virtual void SetOnAvailable(std::function<void()> on_available) = 0;
  virtual void SetOnClosed(std::function<void()> on_closed) = 0;
  virtual void SetOnError(std::function<void(std::string)> on_error) = 0;
  virtual void SetOnMessage(
      std::function<void(const uint8_t*, size_t)> on_message) = 0;
};

std::shared_ptr<DataChannel> CreateDataChannel(
    std::shared_ptr<rtc::DataChannel> dc,
    bool compress);

}  // namespace sorac

#endif
