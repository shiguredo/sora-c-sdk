#include "sorac/data_channel.hpp"

#include <vector>

#include "util.hpp"

namespace sorac {

class DataChannelImpl : public DataChannel {
 public:
  DataChannelImpl(std::shared_ptr<rtc::DataChannel> dc, bool compress)
      : dc_(dc), compress_(compress) {}

  std::string GetLabel() const override { return dc_->label(); }
  bool Send(const uint8_t* buf, size_t size) override {
    rtc::binary data;
    if (compress_) {
      auto compressed = zlib_compress(buf, size);
      data.assign((const std::byte*)compressed.data(),
                  (const std::byte*)compressed.data() + compressed.size());
    } else {
      data.assign((const std::byte*)buf, (const std::byte*)buf + size);
    }
    return dc_->send(data);
  }

  void SetOnOpen(std::function<void()> on_open) override {
    dc_->onOpen(on_open);
  }
  void SetOnAvailable(std::function<void()> on_available) override {
    dc_->onAvailable(on_available);
  }
  void SetOnClosed(std::function<void()> on_closed) override {
    dc_->onClosed(on_closed);
  }
  void SetOnError(std::function<void(std::string)> on_error) override {
    dc_->onError(on_error);
  }
  void SetOnMessage(
      std::function<void(const uint8_t*, size_t)> on_message) override {
    dc_->onMessage([on_message, this](rtc::message_variant data) {
      const uint8_t* buf;
      size_t size;
      if (std::holds_alternative<rtc::binary>(data)) {
        const auto& bin = std::get<rtc::binary>(data);
        buf = (const uint8_t*)bin.data();
        size = bin.size();
      } else {
        const auto& str = std::get<std::string>(data);
        buf = (const uint8_t*)str.data();
        size = str.size();
      }

      if (compress_) {
        std::string r = zlib_uncompress(buf, size);
        buf = (const uint8_t*)r.data();
        size = r.size();
      }

      on_message(buf, size);
    });
  }

 private:
  std::shared_ptr<rtc::DataChannel> dc_;
  bool compress_;
};

std::shared_ptr<DataChannel> CreateDataChannel(
    std::shared_ptr<rtc::DataChannel> dc,
    bool compress) {
  return std::make_shared<DataChannelImpl>(dc, compress);
}

}  // namespace sorac