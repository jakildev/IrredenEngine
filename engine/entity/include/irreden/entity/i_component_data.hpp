#ifndef I_COMPONENT_DATA_H
#define I_COMPONENT_DATA_H

#include <irreden/ir_profile.hpp>

#include <irreden/entity/ir_entity_types.hpp>

#include <vector>
#include <memory>

namespace IREntity {

// TODO: This is a good example of why some standardization for components
// could be good, instead of just having them any struct. Enforcing on this
// level is fine too
template <typename Component> struct HasOnDestroy {
  private:
    template <typename U>
    static constexpr auto test(int) -> decltype(std::declval<U>().onDestroy(), std::true_type());

    template <typename> static constexpr std::false_type test(...);

  public:
    static constexpr bool value = decltype(test<Component>(0))::value;
};

// IR: 119

class IComponentData {
  public:
    virtual ~IComponentData() = default;
    virtual int size() const = 0;
    virtual smart_ComponentData cloneEmpty() const = 0;
    virtual void moveDataAndPack(IComponentData *dest, const int indexSource) = 0;
    virtual void pushCopyData(IComponentData *dest, const int indexSource) = 0;
    virtual void removeDataAndPack(const int index) = 0;
    virtual void destroy(const int index) = 0;
};

template <typename Component> class IComponentDataImpl : public IComponentData {
  public:
    /* TODO: Maybe make this private and use a getter */
    std::vector<Component> dataVector;
    IComponentDataImpl() {}
    virtual ~IComponentDataImpl() {}
    inline virtual int size() const override {
        return dataVector.size();
    }

    std::vector<Component> &getDataVector() const {
        return dataVector;
    }

    virtual smart_ComponentData cloneEmpty() const override {
        return std::make_unique<IComponentDataImpl<Component>>();
    }

    virtual void moveDataAndPack(IComponentData *dest, const int indexSource) override {
        IComponentDataImpl<Component> *castedDest =
            static_cast<IComponentDataImpl<Component> *>(dest);
        castedDest->dataVector.push_back(this->dataVector[indexSource]);
        this->dataVector[indexSource] = this->dataVector.back();
        this->dataVector.pop_back();
    }

    virtual void pushCopyData(IComponentData *dest, const int indexSource) override {
        IComponentDataImpl<Component> *castedDest =
            static_cast<IComponentDataImpl<Component> *>(dest);
        castedDest->dataVector.push_back(this->dataVector[indexSource]);
    }

    virtual void removeDataAndPack(const int index) override {
        IR_ASSERT(index < size(), "Attempted to remove data with index out of bounds");
        this->dataVector[index] = this->dataVector.back();
        this->dataVector.pop_back();
    }

    virtual void destroy(const int index) override {
        // Check if Component has an onDestroy method, if it does call it
        if constexpr (HasOnDestroy<Component>::value) {
            this->dataVector[index].onDestroy();
        }
    }
};

template <typename Component>
IComponentDataImpl<Component> *castComponentDataPointer(IComponentData *data) {
    return static_cast<IComponentDataImpl<Component> *>(data);
}

} // namespace IREntity

#endif /* I_COMPONENT_DATA_H */
