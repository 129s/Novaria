#pragma once

#include "net/net_service.h"

namespace novaria::net {

class NetServiceStub final : public INetService {
public:
    bool Initialize(std::string& out_error) override;
    void Shutdown() override;
    void Tick(const sim::TickContext& tick_context) override;
    void SubmitLocalCommand(const PlayerCommand& command) override;
    void PublishWorldSnapshot(std::uint64_t tick_index) override;

private:
    bool initialized_ = false;
};

}  // namespace novaria::net
