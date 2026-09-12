---
name: ecs-prefab-creator
description: >-
  Creates new ECS prefabs — components, systems, and commands — under
  `engine/prefabs/irreden/<domain>/` following the engine's file, naming, and
  registration conventions. Use when the user wants to add a component,
  system, or command, or asks about ECS prefab structure and patterns.
---

# ECS Prefab Creator

Prefabs are header-only files under `engine/prefabs/irreden/<domain>/`
(`common/`, `update/`, `voxel/`, `render/`, `input/`, `audio/`, `video/`, ...),
compiled only when a creation includes them. File paths and conventions:
[`engine/prefabs/CLAUDE.md`](../../../engine/prefabs/CLAUDE.md) §"File
pattern" and §"Conventions"; naming:
[`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
§Naming.

## Component

`engine/prefabs/irreden/<domain>/components/component_<name>.hpp`, include
guard `COMPONENT_<NAME>_H`, `namespace IRComponents`, struct `C_<PascalName>`,
public members with trailing `_`, a default constructor plus at least one
parameterised constructor:

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

Method tiers a component may carry:
[`.claude/rules/cpp-ecs.md`](../../rules/cpp-ecs.md) §"Component method
tiers".

## System

1. Add a `SCREAMING_SNAKE_CASE` entry to the `SystemName` enum in
   `engine/system/include/irreden/system/ir_system_types.hpp` under the
   matching comment group (Input, Update, Render).
2. `engine/prefabs/irreden/<domain>/systems/system_<name>.hpp`: specialise
   `IRSystem::System<SYSTEM_NAME>` with a static `create()` that calls
   `createSystem<Components...>()`.

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

The three tick signatures (per-component, per-entity-id, per-archetype batch)
and the `beginTick` / `endTick` / `relationTick` / `start` / `stop` hooks:
[`engine/system/CLAUDE.md`](../../../engine/system/CLAUDE.md) §"Three valid
TICK function signatures" and §"Begin/End/Relation ticks". System-owned state
lives on `System<N>` or in `SystemParams`, never a function-local static:
[`.claude/rules/cpp-systems.md`](../../rules/cpp-systems.md). No
`getComponent` / `getComponentOptional` and no structural entity changes inside
a per-entity tick: [`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md)
(checklist) and [`.claude/rules/cpp-ecs.md`](../../rules/cpp-ecs.md)
(template-parameter widening, caching at creation, `relationTick`, batched
foreign-entity lookups).

## Command

1. Add a `SCREAMING_SNAKE_CASE` entry to the `CommandNames` enum in
   `engine/command/include/irreden/command/ir_command_types.hpp`.
2. `engine/prefabs/irreden/<domain>/commands/command_<name>.hpp`: specialise
   `IRCommand::Command<COMMAND_NAME>` with a static `create()` returning a
   callable
   ([`engine/command/CLAUDE.md`](../../../engine/command/CLAUDE.md) §"The
   `Command<NAME>` pattern").

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

#endif /* COMMAND_MY_ACTION_H */
```

Bind it in the creation's `initCommands()` (input types `KEY_MOUSE`,
`MIDI_NOTE`, `MIDI_CC`; optional modifier last):

```cpp
IRCommand::createCommand<IRCommand::MY_ACTION>(
    InputTypes::KEY_MOUSE,
    ButtonStatuses::PRESSED,
    KeyMouseButtons::kKeyButtonF5,
    IRInput::kModifierControl   // optional
);
```

## Registration in a creation

`#include` the new headers; add `createSystem<IRSystem::MY_SYSTEM>()` to the
right `registerPipeline()` call and `createCommand<IRCommand::MY_COMMAND>(...)`
to `initCommands()`. Pipeline order (INPUT → UPDATE → RENDER) and ordering
within a pipeline: [`engine/system/CLAUDE.md`](../../../engine/system/CLAUDE.md)
§Pipelines.

## Done when

- Component: `C_` prefix, `IRComponents` namespace, include guard matches the
  filename.
- System / command: enum entry added first; `System<NAME>` /
  `Command<NAME>` specialisation with `create()`.
- No `getComponent` inside a per-entity tick lambda.
