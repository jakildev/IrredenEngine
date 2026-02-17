#ifndef ENTITY_HANDLE_H
#define ENTITY_HANDLE_H

#include <irreden/ir_entity.hpp>

namespace IREntity {

struct EntityHandle {
    EntityId id_;

    EntityHandle() {
        id_ = getEntityManager().createEntity();
    }

    EntityHandle(EntityId id) {
        id_ = id;
    }

    ~EntityHandle() {}

    EntityId getId() const {
        return id_;
    }

    EntityHandle(const EntityHandle &src) {
        *this = src;
    }

    EntityHandle &operator=(EntityHandle &&src) {
        if (this == &src) {
            return *this;
        }
        id_ = src.id_;
        return *this;
    }

    EntityHandle(EntityHandle &&src) {
        *this = std::move(src);
    }

    EntityHandle &operator=(const EntityHandle &src) {
        id_ = src.id_;
        return *this;
    }

    bool operator==(const IRECS::EntityHandle &rhs) {
        return id_ == rhs.id_;
    }

    template <typename T> T &set(T component) const {
        T &res = getEntityManager().setComponent(id_, component);
        return res;
    }

    template <typename... Ts> void setMultiple(Ts... components) {
        getEntityManager().setComponents(id_, components...);
    }

    template <typename T> T &get() const {
        T &res = getEntityManager().getComponent<T>(id_);
        return res;
    }

    template <typename T> T &updateComponent(T component) const {
        T &res = this->get<T>();
        res = component;
        return res;
    }

    template <typename T> void remove() {
        getEntityManager().removeComponent<T>(id_);
    }

    void destroyDeferred() {
        getEntityManager().markEntityForDeletion(id_);
    }

    // TODO: Perhaps should only use destroy defered...
    void destroy() {
        getEntityManager().destroyEntity(id_);
    }
};

} // namespace IREntity

#endif /* ENTITY_HANDLE_H */
