#include "sorac/simulcast_media_handler.hpp"

// plog
#include <plog/Log.h>

namespace sorac {

SimulcastMediaHandler::SimulcastMediaHandler(
    std::shared_ptr<SimulcastMediaHandlerConfig> config)
    : config_(config) {}

void SimulcastMediaHandler::addToChainWithRid(
    std::string rid,
    std::shared_ptr<rtc::MediaHandler> handler,
    std::function<std::function<void(std::string rid)>(std::string rid)>
        prepare) {
  rid_handlers_[rid] = RidHandler{prepare, handler};
}

void SimulcastMediaHandler::incoming(rtc::message_vector& messages,
                                     const rtc::message_callback& send) {
  // とりあえず全ての MediaHandler に送る
  // rid 付きで受信した場合に何かするかもしれない
  for (auto& handler : rid_handlers_) {
    handler.second.handler->incomingChain(messages, send);
  }
}

void SimulcastMediaHandler::outgoing(rtc::message_vector& messages,
                                     const rtc::message_callback& send) {
  // rid が無い場合、全ての MediaHandler に送る
  // rid がある場合、rid に応じてそれぞれの MediaHandler に送る
  if (config_->rid == std::nullopt) {
    for (auto& handler : rid_handlers_) {
      handler.second.handler->outgoingChain(messages, send);
    }
  } else {
    auto rid = config_->rid.value();
    auto handler = rid_handlers_[rid];
    auto cleanup = handler.prepare(rid);
    handler.handler->outgoingChain(messages, send);
    if (cleanup) {
      cleanup(rid);
    }
  }
}

std::shared_ptr<SimulcastMediaHandlerConfig> SimulcastMediaHandler::config()
    const {
  return config_;
}

}  // namespace sorac