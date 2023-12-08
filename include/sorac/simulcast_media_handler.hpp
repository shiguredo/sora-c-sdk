#ifndef SORAC_SIMULCAST_MEDIA_HANDLER_HPP_
#define SORAC_SIMULCAST_MEDIA_HANDLER_HPP_

#include <optional>

// libdatachannel
#include <rtc/rtc.hpp>

namespace sorac {

struct SimulcastMediaHandlerConfig {
  std::optional<std::string> rid;
};

class SimulcastMediaHandler : public rtc::MediaHandler {
 public:
  SimulcastMediaHandler(std::shared_ptr<SimulcastMediaHandlerConfig> config);

  void addToChainWithRid(
      std::string rid,
      std::shared_ptr<rtc::MediaHandler> handler,
      std::function<std::function<void(std::string rid)>(std::string rid)>
          prepare);

  void incoming(rtc::message_vector& messages,
                const rtc::message_callback& send) override;
  void outgoing(rtc::message_vector& messages,
                const rtc::message_callback& send) override;

  std::shared_ptr<SimulcastMediaHandlerConfig> config() const;

 private:
  struct RidHandler {
    std::function<std::function<void(std::string rid)>(std::string rid)>
        prepare;
    std::shared_ptr<rtc::MediaHandler> handler;
  };
  std::map<std::string, RidHandler> rid_handlers_;
  std::shared_ptr<SimulcastMediaHandlerConfig> config_;
};

}  // namespace sorac

#endif
