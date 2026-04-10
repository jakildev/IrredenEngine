---
name: ecs-prefab-creator
description: >-
  Create new ECS prefabs (components, systems, commands) for Irreden Engine
  following established conventions. Use when the user wants to add a new
  component, system, or command, or asks about ECS prefab structure and
  patterns.
---

# ECS Prefab Creator

## Overview

Prefabs are header-only files under `engine/prefabs/irreden/<domain>/`. They compile only when included by a creation. Domains include `common/`, `update/`, `voxel/`, `render/`, `input/`, `audio/`, `video/`.

## Creating a Component

**File:** `engine/prefabs/irreden/<domain>/components/component_<name>.hpp`

1. Use include guard `COMPONENT_<NAME>_H`.
2. Namespace `IRComponents`, struct name `C_<PascalName>`.
3. Public members use trailing `_`. Private members use `m_` prefix.
4. Provide a default constructor and at least one parameterized constructor.

```cpp
#ifndef COMPONENT_MOVE_ORDER_H
#define COMPONENT_MOVE_ORDER_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_MoveOrder {
    ivec3 targetCell_;

    C_MoveOrder()
        : targetCell_{0, 0, 0} {}

    C_MoveOrder(ivec3 targetCell)
        : targetCell_{targetCell} {}
};

} // namespace IRComponents

#endif /* COMPONENT_MOVE_ORDER_H */
```

## Creating a System

**Two required steps:**

### Step 1: Register the system name

Add a `SCREAMING_SNAKE_CASE` entry to the `SystemName` enum in `engine/system/include/irreden/system/ir_system_types.hpp`, placed under the appropriate comment group (Input, Update, or Render).

### Step 2: Write the system header

**File:** `engine/prefabs/irreden/<domain>/systems/system_<name>.hpp`

Specialize `IRSystem::System<SYSTEM_NAME>` with a static `create()` that calls `createSystem<Components...>()`.

### Tick signatures

**Per-component** (most common — iterates dense column storage):

```cpp
#ifndef SYSTEM_VELOCITY_DRAG_H
#define SYSTEM_VELOCITY_DRAG_H

#include <irreden/ir_system.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_velocity_drag.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<VELOCITY_DRAG> {
    static SystemId create() {
        return createSystem<C_Velocity3D, C_VelocityDrag>(
            "VelocityDrag",
            [](C_Velocity3D &velocity, const C_VelocityDrag &drag) {
                velocity.velocity_ *= (1.0f - drag.drag_ * IRTime::deltaTime(IRTime::UPDATE));
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VELOCITY_DRAG_H */
```

**Per-entity-id** (when you need to query other components by entity):

```cpp
createSystem<C_NavAgent, C_MoveOrder>(
    "GridPathfind",
    [](EntityId id, C_NavAgent &agent, C_MoveOrder &order) {
        // id is available for relation lookups or named-entity access
    }
);
```

**Per-archetype-node / batch** (bulk or SIMD-style processing):

```cpp
createSystem<C_NavAgent>(
    "GridBake",
    [](const Archetype &arch, std::vector<EntityId> &ids,
       std::vector<C_NavAgent> &agents) {
        // operate on contiguous vectors
    }
);
```

### System events

Systems support multiple event hooks via optional lambdas after the tick function:

| Event | Purpose |
|-------|---------|
| `beginTick` | Runs once before per-entity iteration each frame |
| `endTick` | Runs once after all entities are processed |
| `relationTick` | Fires once per unique parent entity (for `CHILD_OF` etc.) |
| `start` / `stop` | Lifecycle hooks |

### Anti-patterns

- **Never** call `getComponent` or `getComponentOptional` inside a per-entity tick. Each call does hash-map + linear scan + hash-map lookups with profiler overhead.
- Instead: include the component in the system's template parameters, store data at creation time, use `beginTick`/`endTick` for once-per-frame lookups, or use `relationTick` for per-parent-group lookups.

## Creating a Command

**Two required steps:**

### Step 1: Register the command name

Add a `SCREAMING_SNAKE_CASE` entry to the `CommandNames` enum in `engine/command/include/irreden/command/ir_command_types.hpp`.

### Step 2: Write the command header

**File:** `engine/prefabs/irreden/<domain>/commands/command_<name>.hpp`

Specialize `IRCommand::Command<COMMAND_NAME>` with a static `create()` returning a callable:

```cpp
#ifndef COMMAND_MY_ACTION_H
#define COMMAND_MY_ACTION_H

#include <irreden/ir_command.hpp>

namespace IRCommand {

template <> struct Command<MY_ACTION> {
    static auto create() {
        return []() {
            // action logic
        };
    }
};

} // namespace IRCommand
```

### Binding a command to input (in the creation's `initCommands`):

```cpp
IRCommand::createCommand<IRCommand::MY_ACTION>(
    InputTypes::KEY_MOUSE,
    ButtonStatuses::PRESSED,
    KeyMouseButtons::kKeyButtonF5
);

// With modifier:
IRCommand::createCommand<IRCommand::MY_ACTION>(
    InputTypes::KEY_MOUSE,
    ButtonStatuses::PRESSED,
    KeyMouseButtons::kKeyButtonF5,
    IRInput::kModifierControl
);
```

Command types: `KEY_MOUSE`, `MIDI_NOTE`, `MIDI_CC`.

## Registration in a Creation

After creating prefabs, the creation must:

1. `#include` the new system/command headers.
2. Add `createSystem<IRSystem::MY_SYSTEM>()` in the appropriate `registerPipeline()` call.
3. Add `createCommand<IRCommand::MY_COMMAND>(...)` in `initCommands()`.

Pipelines run in order: **INPUT -> UPDATE -> RENDER**. System order within a pipeline matters.

## Checklist

- [ ] Component: `C_` prefix, `IRComponents` namespace, trailing `_` on public members
- [ ] System: enum added to `ir_system_types.hpp` first
- [ ] System: `System<NAME>` specialization with `create()`
- [ ] Command: enum added to `ir_command_types.hpp` first
- [ ] Command: `Command<NAME>` specialization with `create()`
- [ ] Include guard matches filename convention
- [ ] No `getComponent` calls inside per-entity tick lambdas
