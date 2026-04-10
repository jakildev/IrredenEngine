#ifndef COMMAND_TOGGLE_CULLING_FREEZE_H
#define COMMAND_TOGGLE_CULLING_FREEZE_H

#include <irreden/ir_command.hpp>

namespace IRCommand {

namespace detail {
inline bool &cullingFreezeFlag() {
    static bool frozen = false;
    return frozen;
}
} // namespace detail

inline bool isCullingFrozen() {
    return detail::cullingFreezeFlag();
}

template <> struct Command<TOGGLE_CULLING_FREEZE> {
    static auto create() {
        return []() {
            detail::cullingFreezeFlag() = !detail::cullingFreezeFlag();
        };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_CULLING_FREEZE_H */
