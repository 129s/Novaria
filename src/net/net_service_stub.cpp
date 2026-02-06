#include "net/net_service_stub.h"

#include "core/logger.h"

namespace novaria::net {

bool NetServiceStub::Initialize(std::string& out_error) {
    initialized_ = true;
    out_error.clear();
    core::Logger::Info("net", "Net service stub initialized.");
    return true;
}

void NetServiceStub::Shutdown() {
    if (!initialized_) {
        return;
    }

    initialized_ = false;
    core::Logger::Info("net", "Net service stub shutdown.");
}

void NetServiceStub::Tick(const sim::TickContext& tick_context) {
    (void)tick_context;
}

void NetServiceStub::SubmitLocalCommand(const PlayerCommand& command) {
    (void)command;
}

void NetServiceStub::PublishWorldSnapshot(std::uint64_t tick_index) {
    (void)tick_index;
}

}  // namespace novaria::net
