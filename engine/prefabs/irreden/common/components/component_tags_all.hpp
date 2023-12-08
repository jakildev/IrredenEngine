/*
 * Project: Irreden Engine
 * File: component_tags_all.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_TAGS_ALL_H
#define COMPONENT_TAGS_ALL_H

namespace IRComponents {

    struct C_MarkedForDeletion{}; // unused
    struct C_ChunkVisibleThisFrame{};
    struct C_WallBounce{};
    struct C_WallDeath{};
    struct C_ActiveHitbox{};
    struct C_GuiElement{};
    struct C_MidiIn{};
    struct C_MidiOut{};
    struct C_HasGravity{};
    // struct C_MainCanvas{};
    // struct C_GuiCanvas{};
    // struct C_BackgroundCanvas{};
    struct C_Loop{};
    struct C_IsNotPure{}; // TEMP while not executing on entities with one component
    // struct C_IsGamepad{};

}

#endif /* COMPONENT_TAGS_ALL_H */
