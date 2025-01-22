#pragma once

#include <memory>

namespace IRRender {

    class RenderImpl {
    public:
        virtual ~RenderImpl() = default;
        virtual void init() = 0;
        virtual void printInfo() = 0;
    };

    std::unique_ptr<RenderImpl> createRenderer();

} // namespace IRRender