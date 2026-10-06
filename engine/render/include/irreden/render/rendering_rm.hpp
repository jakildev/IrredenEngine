#ifndef RENDERING_RM_H
#define RENDERING_RM_H

#include <irreden/ir_profile.hpp>

#include <irreden/render/ir_render_types.hpp>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <memory>

namespace IRRender {

template <typename T> using ResourceMap = std::unordered_map<ResourceId, std::unique_ptr<T>>;

constexpr ResourceId IR_MAX_RESOURCES = 0xFFFFF;

class ResourceData {
  public:
    virtual ~ResourceData() = default;
    virtual int size() const = 0;
};

template <typename T> class ResourceDataImpl : public ResourceData {
  public:
    ResourceMap<T> resourceMap;
    ResourceDataImpl() {}
    virtual ~ResourceDataImpl() {}
    inline virtual int size() const override {
        return resourceMap.size();
    }
};

class RenderingResourceManager {
  public:
    // `idCapacity` is the number of ids the pool is seeded with, and so the
    // ceiling on simultaneously live resources.
    explicit RenderingResourceManager(ResourceId idCapacity = IR_MAX_RESOURCES);
    ~RenderingResourceManager();

    int liveResourceCount() const {
        return m_liveResourceCount;
    }
    std::size_t freeIdCount() const {
        return m_resourcePool.size();
    }

    // Ids are handed out FIFO, so a destroyed id is reused only after every
    // other free id. Exhausting the pool asserts; under IR_RELEASE, where the
    // assert compiles out, it aborts rather than read an empty queue.
    template <typename T, typename... Args> std::pair<ResourceId, T *> create(Args &&...args) {
        IR_ASSERT(
            !m_resourcePool.empty(),
            "Resource id pool exhausted at {} live resources (default ceiling IR_MAX_RESOURCES={})",
            m_liveResourceCount,
            IR_MAX_RESOURCES
        );
        if (m_resourcePool.empty()) {
            std::abort();
        }
        ResourceId id = m_resourcePool.front();
        m_resourcePool.pop();
        ResourceType type = getResourceType<T>();
        ResourceDataImpl<T> *container =
            static_cast<ResourceDataImpl<T> *>(m_resourceMaps[type].get());
        auto res =
            container->resourceMap.emplace(id, std::make_unique<T>(std::forward<Args>(args)...));
        m_liveResourceCount++;
        IRE_LOG_INFO("Created ResourceId={}, type={}", id, type);
        return std::pair(id, res.first->second.get());
    }

    template <typename T, typename... Args>
    std::pair<ResourceId, T *> createNamed(const std::string &name, Args &&...args) {
        IR_ASSERT(!m_namedResources.contains(name), "Resource name already exists: {}", name);
        auto result = create<T>(std::forward<Args>(args)...);
        m_namedResources.insert({name, result.first});
        IRE_LOG_INFO(" Resource {} named {}", result.first, name);
        return result;
    }

    template <typename T> T *get(ResourceId resource) {
        ResourceType type = getResourceType<T>();
        ResourceDataImpl<T> *container =
            static_cast<ResourceDataImpl<T> *>(m_resourceMaps[type].get());
        auto it = container->resourceMap.find(resource);
        IR_ASSERT(it != container->resourceMap.end(), "Failed to find resource: {}", resource);
        return it->second.get();
    }

    // Asserts on an unregistered name; never returns null, so a null check on
    // the result is dead code. Under IR_RELEASE the assert compiles out and the
    // miss becomes an end-iterator dereference, which a null check would not
    // catch either. Callers whose contract is "no-op when the resource is
    // absent" want getNamedOrNull.
    template <typename T> T *getNamed(const std::string &name) {
        auto it = m_namedResources.find(name);
        IR_ASSERT(it != m_namedResources.end(), "Failed to find named resource: {}", name);
        return get<T>(it->second);
    }

    // Probe form of getNamed: null when `name` was never registered, in every
    // build. For a genuinely optional resource — one a creation owns only if it
    // registered the system that creates it.
    template <typename T> T *getNamedOrNull(const std::string &name) {
        auto it = m_namedResources.find(name);
        if (it == m_namedResources.end()) {
            return nullptr;
        }
        return get<T>(it->second);
    }

    // Returns the id to the pool and drops any name registered for it, so a
    // later resource reusing the id is never reachable under the old name.
    // An id that is not a live `T` is rejected: it must not enter the pool a
    // second time, or two live resources would end up sharing it.
    template <typename T> void destroy(ResourceId resource) {
        ResourceType type = getResourceType<T>();
        ResourceDataImpl<T> *container =
            static_cast<ResourceDataImpl<T> *>(m_resourceMaps[type].get());
        if (container->resourceMap.erase(resource) == 0) {
            IRE_LOG_ERROR("Destroy of ResourceId={} that is not live for type={}", resource, type);
            return;
        }
        std::erase_if(m_namedResources, [resource](const auto &named) {
            return named.second == resource;
        });
        m_resourcePool.push(resource);
        m_liveResourceCount--;
    }

  private:
    std::queue<ResourceId> m_resourcePool;
    int m_liveResourceCount = 0;
    // unique_ptr is required for polymorphic ResourceDataImpl<T> storage.
    std::unordered_map<ResourceType, std::unique_ptr<ResourceData>> m_resourceMaps;
    std::unordered_map<std::string, ResourceType> m_resourceTypes;
    std::unordered_map<std::string, ResourceId> m_namedResources;
    ResourceType m_nextResourceType = 0;

    template <typename T> void registerResource() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        std::string typeName = typeid(T).name();
        IR_ASSERT(
            m_resourceTypes.find(typeName) == m_resourceTypes.end(),
            "Regestering the same component twice"
        );
        m_resourceTypes.insert({typeName, m_nextResourceType});
        m_resourceMaps.emplace(m_nextResourceType, std::make_unique<ResourceDataImpl<T>>());
        IRE_LOG_INFO("Registered resource type {} with ID={}", typeName, m_nextResourceType);
        m_nextResourceType++;
    }

    template <typename T> ResourceType getResourceType() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        std::string typeName = typeid(T).name();
        IR_ASSERT(
            m_resourceTypes.find(typeName) != m_resourceTypes.end(),
            "Attempted to find a non-existent resource"
        );

        return m_resourceTypes[typeName];
    }
};

} // namespace IRRender

#endif /* RENDERING_RM_H */
