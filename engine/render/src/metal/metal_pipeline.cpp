#include <irreden/render/shader.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_utility.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace IRRender {

namespace {

std::string metalShaderFileForStage(const ShaderStage &stage) {
    const std::filesystem::path glslPath(stage.getFilepath());
    std::string filename = glslPath.filename().string();
    if (stage.getType() == ShaderType::VERTEX || stage.getType() == ShaderType::FRAGMENT) {
        if (filename.size() > 2) {
            filename = filename.substr(2);
        }
    }
    const std::filesystem::path metalPath =
        glslPath.parent_path() / "metal" / std::filesystem::path(filename).replace_extension(".metal");
    return metalPath.string();
}

std::string metalFunctionNameForStage(const ShaderStage &stage) {
    return std::filesystem::path(stage.getFilepath()).stem().string();
}

MTL::Size threadgroupSizeForFunctionName(const std::string &functionName) {
    if (functionName == "c_voxel_to_trixel_stage_1" ||
        functionName == "c_voxel_to_trixel_stage_2") {
        return MTL::Size(2, 3, 1);
    }
    if (functionName == "c_text_to_trixel") {
        return MTL::Size(7, 11, 1);
    }
    if (functionName == "c_shapes_to_trixel") {
        return MTL::Size(8, 8, 1);
    }
    if (functionName == "c_voxel_visibility_compact" ||
        functionName == "c_update_voxel_positions") {
        return MTL::Size(64, 1, 1);
    }
    if (functionName == "c_trixel_to_trixel") {
        return MTL::Size(16, 16, 1);
    }
    if (functionName == "c_lighting_to_trixel") {
        return MTL::Size(16, 16, 1);
    }
    if (functionName == "c_compute_voxel_ao") {
        return MTL::Size(16, 16, 1);
    }
    return MTL::Size(1, 1, 1);
}

// Minimal #include "name.metal" preprocessor.  Resolves header references
// against the directory of the file currently being parsed and prevents
// infinite recursion via a visited set.  We need this because Metal's
// newLibraryWithSource: API does not perform user-include resolution.
std::string loadAndPreprocessMetalSource(
    const std::string &filepath,
    std::unordered_set<std::string> &visited
) {
    const std::string canonical = std::filesystem::weakly_canonical(filepath).string();
    if (visited.count(canonical) != 0) {
        return std::string{};
    }
    visited.insert(canonical);

    const std::string source = IRUtility::readFileAsString(filepath);
    const std::filesystem::path baseDir =
        std::filesystem::path(filepath).parent_path();

    std::string out;
    out.reserve(source.size());

    std::size_t pos = 0;
    while (pos < source.size()) {
        // Find the next newline so we work line-by-line.
        const std::size_t lineEnd = source.find('\n', pos);
        const std::string_view line(
            source.data() + pos,
            (lineEnd == std::string::npos ? source.size() : lineEnd) - pos
        );

        // Check for `#include "name"` after stripping leading whitespace.
        std::size_t cursor = 0;
        while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
            ++cursor;
        }
        constexpr std::string_view kIncludeKeyword = "#include";
        bool handled = false;
        if (cursor + kIncludeKeyword.size() < line.size() &&
            line.substr(cursor, kIncludeKeyword.size()) == kIncludeKeyword) {
            cursor += kIncludeKeyword.size();
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
                ++cursor;
            }
            if (cursor < line.size() && line[cursor] == '"') {
                ++cursor;
                const std::size_t quoteEnd = line.find('"', cursor);
                if (quoteEnd != std::string_view::npos) {
                    const std::string headerName(line.substr(cursor, quoteEnd - cursor));
                    const std::filesystem::path headerPath = baseDir / headerName;
                    out += "// === begin include: " + headerName + " ===\n";
                    out += loadAndPreprocessMetalSource(headerPath.string(), visited);
                    out += "// === end include: " + headerName + " ===\n";
                    handled = true;
                }
            }
        }

        if (!handled) {
            out.append(line.data(), line.size());
            out.push_back('\n');
        }

        if (lineEnd == std::string::npos) {
            break;
        }
        pos = lineEnd + 1;
    }
    return out;
}

MTL::Library *loadMetalLibrary(const std::string &filepath) {
    static std::unordered_map<std::string, MTL::Library *> s_libraryCache;
    const auto cached = s_libraryCache.find(filepath);
    if (cached != s_libraryCache.end()) {
        return cached->second;
    }

    std::unordered_set<std::string> visited;
    const std::string source = loadAndPreprocessMetalSource(filepath, visited);
    NS::Error *error = nullptr;
    NS::String *nsSource = NS::String::string(source.c_str(), NS::UTF8StringEncoding);
    MTL::Library *library = metalDevice()->newLibrary(nsSource, nullptr, &error);
    if (error != nullptr) {
        const char *description =
            error->localizedDescription() != nullptr ? error->localizedDescription()->utf8String()
                                                     : "<unknown>";
        IRE_LOG_FATAL("Metal shader compile failed for '{}': {}", filepath, description);
        IR_ASSERT(false, "Metal shader compile failed.");
    }
    IR_ASSERT(library != nullptr, "Failed to create Metal shader library");
    s_libraryCache[filepath] = library;
    return library;
}

std::uint32_t nextMetalPipelineHandle() {
    static std::uint32_t s_nextHandle = 1;
    return s_nextHandle++;
}

std::string renderPipelineCacheKey(
    MTL::PixelFormat colorPixelFormat,
    MTL::PixelFormat depthPixelFormat,
    const MTL::VertexDescriptor *vertexDescriptor
) {
    return std::to_string(static_cast<int>(colorPixelFormat)) + ":" +
           std::to_string(static_cast<int>(depthPixelFormat)) + ":" +
           std::to_string(reinterpret_cast<std::uintptr_t>(vertexDescriptor));
}

} // namespace

class MetalShaderPipelineImpl final : public ShaderPipelineImpl, public MetalPipelineStateProvider {
  public:
    explicit MetalShaderPipelineImpl(const std::vector<ShaderStage> &stages)
        : m_handle(nextMetalPipelineHandle()) {
        for (const ShaderStage &stage : stages) {
            const std::string metalFile = metalShaderFileForStage(stage);
            const std::string functionName = metalFunctionNameForStage(stage);
            MTL::Library *library = loadMetalLibrary(metalFile);
            NS::String *nsFunctionName = NS::String::string(functionName.c_str(), NS::UTF8StringEncoding);
            MTL::Function *function = library->newFunction(nsFunctionName);
            IR_ASSERT(function != nullptr, "Failed to load Metal shader function");

            switch (stage.getType()) {
                case ShaderType::VERTEX:
                    m_vertexFunction = function;
                    break;
                case ShaderType::FRAGMENT:
                    m_fragmentFunction = function;
                    break;
                case ShaderType::COMPUTE:
                    m_computeFunction = function;
                    m_computeThreadsPerThreadgroup = threadgroupSizeForFunctionName(functionName);
                    break;
                case ShaderType::GEOMETRY:
                    function->release();
                    IR_ASSERT(false, "Metal geometry shaders are not supported");
                    break;
            }
        }
    }

    ~MetalShaderPipelineImpl() override {
        for (auto &[_, pipelineState] : m_renderStates) {
            if (pipelineState != nullptr) {
                pipelineState->release();
            }
        }
        if (m_computeState != nullptr) {
            m_computeState->release();
            m_computeState = nullptr;
        }
        if (m_vertexFunction != nullptr) {
            m_vertexFunction->release();
            m_vertexFunction = nullptr;
        }
        if (m_fragmentFunction != nullptr) {
            m_fragmentFunction->release();
            m_fragmentFunction = nullptr;
        }
        if (m_computeFunction != nullptr) {
            m_computeFunction->release();
            m_computeFunction = nullptr;
        }
    }

    void use() override {
        setActiveMetalPipeline(this);
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    bool isComputePipeline() const override {
        return m_computeFunction != nullptr;
    }

    MTL::Size getThreadsPerThreadgroup() const override {
        return m_computeThreadsPerThreadgroup;
    }

    MTL::RenderPipelineState *getRenderPipelineState(
        MTL::PixelFormat colorPixelFormat,
        MTL::PixelFormat depthPixelFormat,
        const MTL::VertexDescriptor *vertexDescriptor
    ) override {
        if (m_vertexFunction == nullptr || m_fragmentFunction == nullptr || vertexDescriptor == nullptr) {
            return nullptr;
        }
        const std::string key =
            renderPipelineCacheKey(colorPixelFormat, depthPixelFormat, vertexDescriptor);
        const auto cached = m_renderStates.find(key);
        if (cached != m_renderStates.end()) {
            return cached->second;
        }

        auto *descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        descriptor->setVertexFunction(m_vertexFunction);
        descriptor->setFragmentFunction(m_fragmentFunction);
        descriptor->setVertexDescriptor(vertexDescriptor);
        auto *colorAttachment = descriptor->colorAttachments()->object(0);
        colorAttachment->setPixelFormat(colorPixelFormat);
        colorAttachment->setBlendingEnabled(true);
        colorAttachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        colorAttachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        colorAttachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        if (depthPixelFormat != MTL::PixelFormatInvalid) {
            descriptor->setDepthAttachmentPixelFormat(depthPixelFormat);
            descriptor->setStencilAttachmentPixelFormat(depthPixelFormat);
        }

        NS::Error *error = nullptr;
        MTL::RenderPipelineState *pipelineState = metalDevice()->newRenderPipelineState(
            descriptor,
            &error
        );
        descriptor->release();

        if (error != nullptr) {
            const char *description =
                error->localizedDescription() != nullptr ? error->localizedDescription()->utf8String()
                                                         : "<unknown>";
            IRE_LOG_FATAL("Metal render pipeline creation failed: {}", description);
            IR_ASSERT(false, "Metal render pipeline creation failed.");
        }

        IR_ASSERT(pipelineState != nullptr, "Failed to create Metal render pipeline state");
        m_renderStates[key] = pipelineState;
        return pipelineState;
    }

    MTL::ComputePipelineState *getComputePipelineState() override {
        if (m_computeFunction == nullptr) {
            return nullptr;
        }
        if (m_computeState != nullptr) {
            return m_computeState;
        }

        NS::Error *error = nullptr;
        m_computeState = metalDevice()->newComputePipelineState(m_computeFunction, &error);
        if (error != nullptr) {
            const char *description =
                error->localizedDescription() != nullptr ? error->localizedDescription()->utf8String()
                                                         : "<unknown>";
            IRE_LOG_FATAL("Metal compute pipeline creation failed: {}", description);
            IR_ASSERT(false, "Metal compute pipeline creation failed.");
        }
        IR_ASSERT(m_computeState != nullptr, "Failed to create Metal compute pipeline state");
        return m_computeState;
    }

  private:
    std::uint32_t m_handle = 0;
    MTL::Function *m_vertexFunction = nullptr;
    MTL::Function *m_fragmentFunction = nullptr;
    MTL::Function *m_computeFunction = nullptr;
    MTL::ComputePipelineState *m_computeState = nullptr;
    MTL::Size m_computeThreadsPerThreadgroup = MTL::Size(1, 1, 1);
    std::unordered_map<std::string, MTL::RenderPipelineState *> m_renderStates;
};

std::unique_ptr<ShaderPipelineImpl> createShaderPipelineImpl(const std::vector<ShaderStage> &stages) {
    return std::make_unique<MetalShaderPipelineImpl>(stages);
}

} // namespace IRRender
