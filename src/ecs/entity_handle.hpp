/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\entity_handle.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_HANDLE_H
#define ENTITY_HANDLE_H

#include "../world/global.hpp"
#include "entity_manager.hpp"

namespace IRECS {

    struct EntityHandle {
        EntityId id_;
        // IDEA: Use observer pattern and keep stored
        // record with event system

        EntityHandle() {
            //id_ = ENG_ENTITY_MANAGER->createEntity();
            id_ = global.entityManager_->createEntity();
        }

        EntityHandle(EntityId id) {
            id_ = id;
        }

        // template <typename... Components>
        // EntityHandle(Components... components) {
        //     id_ = global.entityManager_->createEntity();
        //     global.entityManager_->setComponents(id_, components...);
        // }


        ~EntityHandle() {

        }

        EntityId getId() const {
            return id_;
        }

        EntityHandle(const EntityHandle& src) {
            *this = src;
        }

        EntityHandle& operator=(EntityHandle&& src) {
            if(this == &src) {
                return *this;
            }
            id_ = src.id_;
            return *this;
        }

        EntityHandle(EntityHandle&& src) {
            *this = std::move(src);
        }

        EntityHandle& operator=(const EntityHandle& src) {
            id_ = src.id_;
            return *this;
        }

        bool operator==(
            const IRECS::EntityHandle& rhs
        )
        {
            return id_ == rhs.id_;
        }

        template <typename T>
        T& set(T component) const {
            // ENG_ENTITY_MANAGER->setComponent(id_, component);
            T& res = global.entityManager_->setComponent(id_, component);
            return res;
        }

        template <typename... Ts>
        void setMultiple(Ts... components) {
            global.entityManager_->setComponents(id_, components...);
        }

        template <typename T>
        T& get() const {
            T& res = global.entityManager_->getComponent<T>(id_);
            return res;
        }

        template <typename T>
        T& updateComponent(T component) const {
            T& res = this->get<T>();
            res = component;
            return res;
        }

        template <typename T>
        void remove() {
            //ENG_ENTITY_MANAGER->removeComponent<T>(id_);
            global.entityManager_->removeComponent<T>(id_);
        }

        // void add(ComponentId component) {
        //     global.entityManager_->addComponent(id_, component);
        // }

        void destroyDeferred()
        {
            global.entityManager_->markEntityForDeletion(id_);
        }

        // TODO: Perhaps should only use destroy defered...
        void destroy() {
            global.entityManager_->destroyEntity(id_);
        }

    };

} // namespace IRECS

#endif /* ENTITY_HANDLE_H */
