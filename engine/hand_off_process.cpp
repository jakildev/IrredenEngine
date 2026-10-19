#include <irreden/ir_engine.hpp>

#include "process_spawn.hpp"

#include <irreden/render/render_device.hpp>

#include <string>
#include <vector>

namespace IREngine::detail {

namespace {

ChildExit runChildToExit(const std::vector<std::string> &argv) {
    SpawnChildRequest request;
    request.argv_ = argv;
    ChildProcess child = spawnChild(request);
    return {child.started(), child.started() ? waitChild(child) : -1, child.error_};
}

class ProcessHandOffPlatform final : public HandOffPlatform {
  public:
    void drainGpu() override {
        IRRender::device()->finish();
    }

    ChildExit runToExit(const std::vector<std::string> &argv) override {
        return runChildToExit(argv);
    }
};

} // namespace

std::unique_ptr<HandOffPlatform> makeProcessHandOffPlatform() {
    return std::make_unique<ProcessHandOffPlatform>();
}

} // namespace IREngine::detail
