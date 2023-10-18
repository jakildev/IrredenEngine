/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\rendering_rm.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Generic resource manager for rendering stuff

#ifndef RENDERING_RM_H
#define RENDERING_RM_H

#include "../profiling/logger_spd.hpp"
#include "../profiling/cpu_profiler.hpp"

#include <glad/glad.h>
#include <vector>
#include <unordered_map>
#include <queue>
#include <memory>
namespace IRRendering {

    typedef uint32_t ResourceId;
    typedef uint32_t ResourceType;
    template <typename T>
    using ResourceMap = std::unordered_map<ResourceId, std::unique_ptr<T>>;

    constexpr ResourceId IR_MAX_RESOURCES = 0xFFFFF;

    class ResourceData {
    public:
        virtual ~ResourceData() = default;
        virtual int size() const = 0;
    };

    template <typename T>
    class ResourceDataImpl : public ResourceData {
    public:
        ResourceMap<T> resourceMap;
        ResourceDataImpl() {

        }
        virtual ~ResourceDataImpl() {}
        inline virtual int size() const override { return resourceMap.size(); }
    };

    class RenderingResourceManager {
    public:
        RenderingResourceManager();

        template <typename T, typename... Args>
        std::pair<ResourceId, T*> create(Args&&... args) {
            ResourceId id = m_resourcePool.front();
            m_resourcePool.pop();
            ResourceType type = getResourceType<T>();
            ResourceDataImpl<T>* container = static_cast<ResourceDataImpl<T>*>(m_resourceMaps[type].get());
            auto res = container->resourceMap.emplace(id, std::make_unique<T>(std::forward<Args>(args)...));
            m_liveResourceCount++;
            ENG_LOG_INFO("Created ResourceId={}, type={}, codepath 2", id, type);
            return std::pair(id, res.first->second.get());
        }

        template <typename T>
        T* get(ResourceId resource) {
            ResourceType type = getResourceType<T>();
            ResourceDataImpl<T>* container = static_cast<ResourceDataImpl<T>*>(m_resourceMaps[type].get());
            auto it = container->resourceMap.find(resource);
            ENG_ASSERT(
                it != container->resourceMap.end(),
                "Failed to find resource."
            );
            return it->second.get();
        }

        template <typename T>
        void destroy(ResourceId resource) {
            ResourceType type = getResourceType<T>();
            ResourceDataImpl<T>* container = static_cast<ResourceDataImpl<T>*>(m_resourceMaps[type].get());
            container->resourceMap.erase(resource);
            m_liveResourceCount--;
        }

    private:
        std::queue<ResourceId> m_resourcePool;
        int m_liveResourceCount = 0;
        std::unordered_map<ResourceType, std::unique_ptr<ResourceData>> m_resourceMaps;
        std::unordered_map<std::string, ResourceType> m_resourceTypes;
        ResourceType m_nextResourceType = 0;

        template <typename T>
        void registerResource() {
            EASY_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
            std::string typeName = typeid(T).name();
            ENG_ASSERT(m_resourceTypes.find(typeName) == m_resourceTypes.end(),
                            "Regestering the same component twice");
            m_resourceTypes.insert({typeName, m_nextResourceType});
            m_resourceMaps.emplace(m_nextResourceType, std::make_unique<ResourceDataImpl<T>>());
            ENG_LOG_INFO("Registered resource type {} with ID={}",
                            typeName,
                            m_nextResourceType);
            m_nextResourceType++;
        }

        template <typename T>
        ResourceType getResourceType() {
            EASY_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
            std::string typeName = typeid(T).name();
            ENG_ASSERT(m_resourceTypes.find(typeName) != m_resourceTypes.end(),
                            "Attempted to find a non-existent resource");

            return m_resourceTypes[typeName];
        }
    };

} // namespace IRRendering

#endif /* RENDERING_RM_H */
