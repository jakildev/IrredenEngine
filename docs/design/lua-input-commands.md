# Lua input & command bindings

Engine-level mechanism that lets a creation declare commands and bind
keyboard / mouse / gamepad input to them entirely from Lua, replacing
the C++ `initCommands()` block that every Lua-driven creation currently
needs. Implementation lands as PR 2 of T-193; this document is the
locked design that PR 2 will follow.

## Table of contents

- [Vocabulary](#vocabulary)
- [Today's surface](#todays-surface)
- [What needs to be exposed](#what-needs-to-be-exposed)
- [Locked design choices](#locked-design-choices)
- [Public Lua API](#public-lua-api)
- [C++ surface deltas](#c-surface-deltas)
- [Dispatch model](#dispatch-model)
- [Lifetime contract](#lifetime-contract)
- [C++ referencing of Lua-defined commands](#c-referencing-of-lua-defined-commands)
- [Out of scope](#out-of-scope)
- [Acceptance for PR 2](#acceptance-for-pr-2)

## Vocabulary

- **Prefab command** — an engine-defined `Command<NAME>` specialization
  ([`engine/prefabs/irreden/input/commands/command_close_window.hpp:10`](../../engine/prefabs/irreden/input/commands/command_close_window.hpp))
  identified by a `CommandNames` enum value
  ([`engine/command/include/irreden/command/ir_command_types.hpp:14`](../../engine/command/include/irreden/command/ir_command_types.hpp)).
  The Lua API lets a creation **bind** an input trigger to a prefab
  command (replacing `createCommand<CommandNames::X>(...)`), and lets
  Lua **fire** a prefab command by name (`Command<X>::create()()`).
- **Lua-defined command** — an ad-hoc command whose body is a
  `sol::protected_function`, registered at script-load time. Identified
  by the runtime `CommandId` returned by `IRCommand::createCommand`
  ([`engine/command/include/irreden/command/ir_command_types.hpp:9`](../../engine/command/include/irreden/command/ir_command_types.hpp)).
  Has no `CommandNames` enum entry; it cannot be `Closes`d, but it can
  be referenced by `CommandId` from both Lua and C++.
- **Input trigger** — the tuple
  `(InputTypes inputType, ButtonStatuses status, int button,
  KeyModifierMask requiredMods, KeyModifierMask blockedMods)`
  passed to `createCommand` and stored inside `CommandStruct<COMMAND_BUTTON>`
  ([`engine/command/include/irreden/command/command_manager.hpp:44`](../../engine/command/include/irreden/command/command_manager.hpp)).

## Today's surface

The command dispatch model has three layers:

1. **`CommandNames` enum** — every prefab command has an entry
   ([`engine/command/include/irreden/command/ir_command_types.hpp:14-51`](../../engine/command/include/irreden/command/ir_command_types.hpp)).
   New entries must be added before the `Command<X>` specialization will
   link.
2. **`Command<NAME>::create()` specialization** — returns a
   `std::function<void()>` (or callable convertible to one). Engine
   prefab commands live under
   `engine/prefabs/irreden/{input,render,video,...}/commands/`.
3. **Registration via `IRCommand::createCommand`** — two overloads in
   [`engine/command/include/irreden/ir_command.hpp:158-197`](../../engine/command/include/irreden/ir_command.hpp):
   - `createCommand<Function>(inputType, status, button, fn, mods)` —
     ad-hoc lambda; returns a `CommandId` (int) that indexes into
     `CommandManager::m_userCommands`.
   - `createCommand<NAME>(inputType, status, button, mods)` — looks up
     `Command<NAME>::create()` and forwards to the lambda overload.

The dispatch loop is once-per-`UPDATE`-event
([`engine/command/src/command_manager.cpp:55-89`](../../engine/command/src/command_manager.cpp)):
for every entry in `m_userCommands`, it checks the input trigger via
`IRInput::checkKeyMouseButton` + `checkKeyMouseModifiers` and calls
`command.execute()` (which invokes the stored `std::function<void()>`).
There is **no per-entity iteration** — the command body is responsible
for whatever ECS work it does, exactly the same shape as a C++ command
lambda.

The relevant input enums live in
[`engine/input/include/irreden/input/ir_input_types.hpp`](../../engine/input/include/irreden/input/ir_input_types.hpp):

- `InputTypes { KEY_MOUSE, GAMEPAD, MIDI_NOTE, MIDI_CC }` (line 27).
- `ButtonStatuses { NOT_HELD, PRESSED, HELD, RELEASED, PRESSED_AND_RELEASED }` (line 24).
- `KeyMouseButtons` (lines 45-174) — keyboard + mouse identifiers.
- `KeyModifierMask` (line 30) + `kModifierShift`/`Control`/`Alt`
  (lines 32-38).
- `GamepadButtons` (lines 179-197), `GamepadAxes` (lines 201-210).

## What needs to be exposed

The 18-call `initCommands()` block at
[`creations/demos/default/main_lua.cpp:107-200`](../../creations/demos/default/main_lua.cpp)
is the canonical migration target. Looking at the 18 calls inside, the
surface required from Lua is:

| Need | Maps to |
|------|---------|
| Bind an input trigger to a prefab command | `IRCommand.bindPrefab(name, inputType, status, button, mods)` |
| Bind an input trigger to a Lua closure | `IRCommand.createCommand(inputType, status, button, fn, mods)` |
| Spell prefab command names | `IRCommand.CommandName.X` (mirrors `IRSystem.SystemName.X`) |
| Spell input enums | `IRInput.InputType.X`, `IRInput.ButtonStatus.X`, `IRInput.Key.X`, `IRInput.Modifier.X`, `IRInput.GamepadButton.X`, `IRInput.GamepadAxis.X` |
| Fire a command imperatively from Lua | `IRCommand.fire(commandIdOrName)` (lets game code trigger commands from systems / event handlers, not just input) |
| Compose modifier masks | bitwise OR of `IRInput.Modifier.X` values — LuaJIT `bit.bor` |

The same surface covers the gamepad + MIDI cases implicitly: the
existing C++ `createCommand` overload takes `InputTypes` as a runtime
enum, so the Lua binding does no work to support `GAMEPAD` /
`MIDI_NOTE` / `MIDI_CC` beyond exposing the enum values.

## Locked design choices

### Q1: Lua-defined command bodies are 1-shot callbacks, not archetype-iterating

The command body is **a `function()` with no arguments**, called once
per match per `UPDATE` event. It is **not** archetype-batched.

Rationale: a prefab `Command<X>::create()` returns
`std::function<void()>`. The Lua surface mirrors that shape so the two
paths are interchangeable. A Lua-defined command body can call any
binding the creation has registered with `LuaScript`. Toy example
using only the engine's standard surface:

```lua
IRCommand.createCommand(
    IRInput.InputType.KEY_MOUSE,
    IRInput.ButtonStatus.PRESSED,
    IRInput.Key.P,
    function()
        IRModifier.addGlobal("GameSpeed.scale", {
            transform = IRModifier.Transform.MULTIPLY,
            value     = 0.5,
            ticks     = 120,
        })
    end
)
```

What a Lua command body can DO is bounded by the Lua bindings the
creation has registered. Today's standard surface (`IRMath`,
`IRComponent`, `IRSystem`, `IRTime`, `IREntity` Lua-component methods,
`IRModifier`, `Prefab`) is fully usable from a command body. A
command that wants to mutate a C++ component on a named entity (the
shape used by `command_move_camera.hpp` at lines 14-19) either needs
that creation-specific binding registered in C++, or should stay as
a C++ prefab command and be **bound** from Lua via `IRCommand.bindPrefab`.

Archetype iteration is what `IRSystem.registerSystem` is for; the two
surfaces stay distinct. A command that needs per-entity work should
register the work as a Lua system and put the per-event trigger into
the command body (e.g. set a singleton `C_Trigger` component that the
system reads next tick).

Per-entity bodies — `function(entity)` triggered for each entity that
matches some filter — are explicitly **rejected for v1**. They duplicate
`IRSystem.registerSystem`'s archetype dispatch with a worse perf shape
(per-entity Lua call across the boundary). Use a system for per-entity
work; a command for per-event side effects.

### Q2: One Lua call → one `CommandStruct<COMMAND_BUTTON>`

A Lua-defined command produces exactly the same entry in
`CommandManager::m_userCommands` that a C++ command would
([`command_manager.hpp:107`](../../engine/command/include/irreden/command/command_manager.hpp)).
The dispatch loop is **unchanged**: it iterates `m_userCommands`,
checks the input trigger, and calls `command.execute()` — which invokes
a `std::function<void()>` that happens to wrap a
`sol::protected_function`. The dispatch loop does not learn about Lua.

This means zero changes to `executeUserKeyboardCommandsAll`
([`command_manager.cpp:55-89`](../../engine/command/src/command_manager.cpp)).
The modifier-specific-match logic (skip bare bindings when a
modifier-qualified binding exists for the same button) applies to Lua
and C++ commands identically.

### Q3: Prefab command names go through one enum table, like `SystemName`

`IRCommand.CommandName.CLOSE_WINDOW` is a `lua_Integer` table populated
by an `IR_BIND_CMD(name)` macro at C++ init time, exactly mirroring
`IRSystem.SystemName.X`
([`engine/script/include/irreden/script/lua_pipeline_bindings.hpp:70-163`](../../engine/script/include/irreden/script/lua_pipeline_bindings.hpp)).
Hand-listed against `CommandNames` so a new prefab command must touch
both files. The drift cost is one line per command and the failure
mode is a clear nil-access error at script load.

Rejected alternatives:

- **Bare strings** (`"CLOSE_WINDOW"`) — drift-prone (no compile-time
  check the string matches the enum), and the cross-file
  search-and-replace cost is the same as the `IR_BIND_CMD` macro.
- **`new_enum` usertype** — would produce typed userdata; nothing
  downstream needs that typing since the value flows through
  `CommandManager::createCommand`'s `template <CommandNames>` overload,
  which already takes the integer. The integer table is consistent
  with `IRSystem.SystemName` and `IRTime` and adds no work.
- **Codegen of the enum table** — overkill for ~35 entries that already
  require manual `Command<X>::create()` specializations. If
  `CommandNames` outgrows the manual table (say, >200 entries), revisit
  via the same codegen extension considered for T-196.

### Q4: A Lua-defined command can be fired by C++ via its `CommandId`

`IRCommand::createCommand` already returns an `int` that indexes into
`m_userCommands`. C++ creations can store the id and invoke
`getCommandManager().m_userCommands[id].execute()` directly. The Lua
return is the same `int`, so the same id round-trips through both
languages.

For v1, add one helper on `CommandManager`:

```cpp
void fireUserCommand(CommandId id);  // bounds-checked execute() of m_userCommands[id]
```

…and an `IRCommand::fire(CommandId)` free function in
`engine/command/include/irreden/ir_command.hpp`. The Lua side gets
`IRCommand.fire(id)` for the same behavior from Lua-driven code.

This is **the only new C++ entry point that PR 2 needs beyond the Lua
binding shim**. Pipeline composition does *not* enter the picture —
commands are not systems, they don't slot into `IRSystem.registerPipeline`.

### Q5: Prefab command names are also fireable from Lua

`IRCommand.fireByName(IRCommand.CommandName.SCREENSHOT)` invokes
`Command<SCREENSHOT>::create()()` directly. Implementation is one
`switch` on `CommandNames` that dispatches to the matching
`Command<NAME>::create()()`. Same hand-listed pattern as
`commandNameToString` ([`ir_command.hpp:21-49`](../../engine/command/include/irreden/ir_command.hpp))
— one line per prefab command, same drift cost. Unlike
`commandNameToString` (which uses `default: return "UNKNOWN"` and
intentionally omits several entries), the `fireByName` switch must
cover every `CommandNames` value; the `default:` case should log an
error and return rather than silently no-op.

This is useful for game-side Lua that wants to trigger an engine
command without an input event (e.g. "screenshot every 5 seconds for
the trailer" or "auto-close on level-end").

Rejected alternative: expose `Command<X>::create()` as a Lua function
factory and let Lua call the resulting `function()`. Two reasons:
(a) the return type is `auto` (depends on the specialization), so
sol2 binding requires casting to `std::function<void()>` at the call
site anyway, and (b) the dispatch table is the simpler shape.

### Q6: Modifier composition uses LuaJIT `bit.bor`, not a varargs constructor

`KeyModifierMask` is `uint8_t` bitwise OR
([`ir_input_types.hpp:30-38`](../../engine/input/include/irreden/input/ir_input_types.hpp)).
LuaJIT 2.1 ships `bit.bor(a, b, c, ...)` natively
([engine/script/CLAUDE.md "Lua runtime: LuaJIT 2.1"](../../engine/script/CLAUDE.md)).
The Lua binding accepts an integer mask directly:

```lua
local mods = bit.bor(IRInput.Modifier.CONTROL, IRInput.Modifier.SHIFT)
IRCommand.bindPrefab(
    IRCommand.CommandName.GUI_ZOOM_IN,
    IRInput.InputType.KEY_MOUSE,
    IRInput.ButtonStatus.PRESSED,
    IRInput.Key.EQUAL,
    mods
)
```

Rejected alternative: an `IRInput.modMask(...)` varargs helper. Adds a
binding for no real value over the native `bit.bor` — LuaJIT optimizes
`bit.*` aggressively, and the `bit.bor` spelling is a hint that this
is a bitmask, not a list.

### Q7: No `--no-allow-c++-rebind` mode; both paths coexist

A creation can mix C++ `initCommands()` and Lua
`IRCommand.bindPrefab` calls. Both paths write into the same
`m_userCommands` vector. The acceptance criterion is that the
`creations/demos/default/main_lua.cpp` block can be moved to Lua
**without removing the C++ surface** — existing creations (including
private ones outside the engine repo) keep working.

The C++ `Command<X>` specializations themselves are kept; what changes
is who calls them. PR 2 does not delete any existing C++ command.

### Q8: Error path — Lua errors inside a command body don't crash

`sol::protected_function::operator()` returns a `sol::protected_function_result`
that carries the error state without throwing. The wrapper:

```cpp
[fn = sol::protected_function{...}, name]() {
    sol::protected_function_result r = fn();
    if (!r.valid()) {
        sol::error e = r;
        IRE_LOG_ERROR("Lua command body '{}' raised: {}", name, e.what());
    }
}
```

A Lua error in one command's body does not abort the dispatch loop or
shut down `LuaScript`. The error is logged and the next command's
trigger check still runs.

The `SOL_EXCEPTIONS_ALWAYS_UNSAFE=1` flag set on `IrredenEngineScripting`
([engine/script/CLAUDE.md "Build flag: SOL_EXCEPTIONS_ALWAYS_UNSAFE"](../../engine/script/CLAUDE.md))
ensures `e.what()` carries the actual Lua message rather than a
generic `"C++ exception"`.

## Public Lua API

```lua
-- Bind a prefab command (most common — replaces the C++ initCommands block):
IRCommand.bindPrefab(
    commandName,    -- IRCommand.CommandName.X (lua_Integer)
    inputType,      -- IRInput.InputType.{KEY_MOUSE,GAMEPAD,MIDI_NOTE,MIDI_CC}
    status,         -- IRInput.ButtonStatus.{PRESSED,HELD,RELEASED,PRESSED_AND_RELEASED,NOT_HELD}
    button,         -- IRInput.Key.X / IRInput.GamepadButton.X / MIDI note/CC integer
    requiredMods,   -- optional, default 0; bit.bor of IRInput.Modifier.{SHIFT,CONTROL,ALT}
    blockedMods     -- optional, default 0; same shape
) -> CommandId

-- Register a Lua-defined command with a closure body:
IRCommand.createCommand(
    inputType, status, button, fn, requiredMods, blockedMods
) -> CommandId

-- Fire a registered command (bindPrefab or createCommand) by id:
IRCommand.fire(commandId)              -- commandId from bindPrefab / createCommand

-- Fire a prefab command by name without a registered binding:
IRCommand.fireByName(commandName)      -- commandName from IRCommand.CommandName
-- Two separate functions because CommandId (m_userCommands vector index,
-- starts at 0) and CommandNames (enum value, also starts at 0) overlap;
-- a single polymorphic call would need a tag and adds no real ergonomic
-- benefit over the two-function shape. Mirrors the C++ split between
-- IRCommand::fire and IRCommand::fireByName.

-- Enum tables (idempotent, populated by C++ binding code):
IRCommand.CommandName        -- { CLOSE_WINDOW=6, ZOOM_IN=2, ... }
IRInput.InputType            -- { KEY_MOUSE=0, GAMEPAD=1, MIDI_NOTE=2, MIDI_CC=3 }
IRInput.ButtonStatus         -- { NOT_HELD=0, PRESSED=1, HELD=2, RELEASED=3, PRESSED_AND_RELEASED=4 }
IRInput.Key                  -- { A=..., B=..., F1=..., SPACE=..., MINUS=..., EQUAL=..., GRAVE=..., ... }
IRInput.Modifier             -- { SHIFT=1, CONTROL=2, ALT=4 }
IRInput.GamepadButton        -- { A=..., B=..., LEFT_BUMPER=..., D_PAD_UP=..., ... }
IRInput.GamepadAxis          -- { LEFT_X=..., LEFT_Y=..., RIGHT_X=..., RIGHT_Y=..., LEFT_TRIGGER=..., RIGHT_TRIGGER=... }
```

The full migrated equivalent of `main_lua.cpp:107-200`:

```lua
local CN = IRCommand.CommandName
local IT = IRInput.InputType
local BS = IRInput.ButtonStatus
local K = IRInput.Key
local CTRL = IRInput.Modifier.CONTROL

IRCommand.bindPrefab(CN.CLOSE_WINDOW,           IT.KEY_MOUSE, BS.PRESSED, K.ESCAPE)
IRCommand.bindPrefab(CN.ZOOM_IN,                IT.KEY_MOUSE, BS.PRESSED, K.EQUAL)
IRCommand.bindPrefab(CN.ZOOM_OUT,               IT.KEY_MOUSE, BS.PRESSED, K.MINUS)
IRCommand.bindPrefab(CN.MOVE_CAMERA_DOWN_START, IT.KEY_MOUSE, BS.PRESSED, K.S)
IRCommand.bindPrefab(CN.MOVE_CAMERA_UP_START,   IT.KEY_MOUSE, BS.PRESSED, K.W)
IRCommand.bindPrefab(CN.MOVE_CAMERA_RIGHT_START,IT.KEY_MOUSE, BS.PRESSED, K.D)
IRCommand.bindPrefab(CN.MOVE_CAMERA_LEFT_START, IT.KEY_MOUSE, BS.PRESSED, K.A)
IRCommand.bindPrefab(CN.MOVE_CAMERA_DOWN_END,   IT.KEY_MOUSE, BS.RELEASED, K.S)
IRCommand.bindPrefab(CN.MOVE_CAMERA_UP_END,     IT.KEY_MOUSE, BS.RELEASED, K.W)
IRCommand.bindPrefab(CN.MOVE_CAMERA_RIGHT_END,  IT.KEY_MOUSE, BS.RELEASED, K.D)
IRCommand.bindPrefab(CN.MOVE_CAMERA_LEFT_END,   IT.KEY_MOUSE, BS.RELEASED, K.A)
IRCommand.bindPrefab(CN.SCREENSHOT,             IT.KEY_MOUSE, BS.PRESSED, K.F8)
IRCommand.bindPrefab(CN.SCREENSHOT_CANVAS,      IT.KEY_MOUSE, BS.PRESSED, K.F7)
IRCommand.bindPrefab(CN.RECORD_TOGGLE,          IT.KEY_MOUSE, BS.PRESSED, K.F9)
IRCommand.bindPrefab(CN.TOGGLE_GUI,             IT.KEY_MOUSE, BS.PRESSED, K.GRAVE)
IRCommand.bindPrefab(CN.GUI_ZOOM_IN,            IT.KEY_MOUSE, BS.PRESSED, K.EQUAL, CTRL)
IRCommand.bindPrefab(CN.GUI_ZOOM_OUT,           IT.KEY_MOUSE, BS.PRESSED, K.MINUS, CTRL)
IRCommand.bindPrefab(CN.TOGGLE_CULLING_FREEZE,  IT.KEY_MOUSE, BS.PRESSED, K.F10)
```

The `K` / `IT` / `BS` / `CTRL` local aliases are convention, not part
of the API. Compare to the original C++ at
[`creations/demos/default/main_lua.cpp:107-200`](../../creations/demos/default/main_lua.cpp)
— same shape, same line count, no `#include` overhead.

## C++ surface deltas

PR 2 changes the C++ engine as follows. All other files in
`engine/command/`, `engine/input/`, `engine/script/` are unchanged.

### `engine/command/include/irreden/command/command_manager.hpp`

Add one method:

```cpp
void fireUserCommand(CommandId id);  // bounds-checked execute() of m_userCommands[id]
```

Implementation in `command_manager.cpp`: bounds-check id against
`m_userCommands.size()`, log + return if out of range, otherwise call
`m_userCommands[id].execute()`.

**Type alignment note (PR 2):** `ir_command_types.hpp:9` declares
`using CommandId = std::uint32_t`, but the existing `createCommand`
overloads return `int`. PR 2 must pick one and be consistent: either
update the existing `createCommand` returns to `CommandId` (`uint32_t`),
or keep `fireUserCommand` taking `int` to match the current callers.
A sign-conversion at realistic vector sizes is not a runtime bug, but
a uniform type across the whole surface is cleaner.

### `engine/command/include/irreden/ir_command.hpp`

Add two free functions:

```cpp
void fire(CommandId id);                  // forwards to getCommandManager().fireUserCommand(id)
void fireByName(CommandNames name);       // switch on name → Command<NAME>::create()()
```

The `fireByName` switch table follows the same hand-listed shape as
`commandNameToString` (lines 21-49) and lives next to it. Unlike
`commandNameToString` (which has a `default: return "UNKNOWN"` catchall
and intentionally omits some entries), `fireByName` must cover every
`CommandNames` value; the `default:` case should log an error and
return rather than silently no-op. Adding a new prefab command requires
adding cases to **both** switches, same as today.

### `engine/script/include/irreden/script/lua_command_bindings.hpp` (new)

Header-only, single-include, follows the `lua_pipeline_bindings.hpp`
pattern. Three `inline void bind*(LuaScript&)` functions:

1. `bindCommandNameEnum` — populates `IRCommand.CommandName` table via
   an `IR_BIND_CMD(name)` macro, hand-listed against `CommandNames`.
2. `bindInputEnums` — populates `IRInput.{InputType, ButtonStatus, Key,
   Modifier, GamepadButton, GamepadAxis}` tables via `IR_BIND_*` macros
   against `InputTypes`/`ButtonStatuses`/`KeyMouseButtons`/etc.
3. `bindCommandFunctions` — exposes
   `IRCommand.{bindPrefab, createCommand, fire, fireByName}` as `sol::function`s.

A new `LuaScript` member `bindLuaCommands()` calls all three. Creations
opt in by calling it after `bindLuaDrivenEcs()`:

```cpp
m_lua.bindLuaDrivenEcs();
m_lua.bindLuaCommands();
m_lua.scriptFile("main.lua");
```

The split is intentional: `bindLuaDrivenEcs` already exposes the
component / system / pipeline surface; `bindLuaCommands` is the
input-and-command surface. A creation that doesn't want Lua commands
just doesn't call `bindLuaCommands`.

### `engine/system/include/irreden/system/ir_system_types.hpp`

No changes. Commands are not systems. The existing
`INPUT_KEY_MOUSE` system already calls
`CommandManager::executeUserKeyboardCommandsAll` per UPDATE; that
system runs Lua-defined and C++-defined commands identically.

## Dispatch model

The dispatch loop runs **once per UPDATE event**, not once per pipeline
or once per archetype. Specifically:

```
TimeManager::tickUPDATE
  → SystemManager runs INPUT_KEY_MOUSE
    → InputManager::advanceInputState(UPDATE)  (sets per-event button states)
    → CommandManager::executeUserKeyboardCommandsAll
      → for each command in m_userCommands:
          if (input trigger matches): command.execute()
```

A Lua-defined command's body fires in the same place a C++ command's
body fires, on the same `INPUT_KEY_MOUSE` tick, with the same
modifier-specific-match precedence rules
([`command_manager.cpp:55-89`](../../engine/command/src/command_manager.cpp)).

Performance characteristics:

- Each Lua command body call costs **one `sol::protected_function`
  invocation** ≈ one `lua_pcall`. LuaJIT compiles short trace-stable
  command bodies after a few invocations.
- The wrapping `std::function<void()>` adds one indirect call vs a
  hand-written C++ lambda — the same indirection a C++ `Command<X>`
  has today via `CommandStruct::m_command_`.
- No per-frame allocations. The `sol::protected_function` is captured
  by value into the lambda; the lambda is captured by value into the
  `std::function`; both are move-constructed once at registration time.

The input trigger check is identical to today (no Lua involvement). The
only Lua-aware code path is `command.execute()` → wrapper lambda →
`sol::protected_function::operator()`.

## Lifetime contract

Mirrors `IRSystem::registerSystem`. The contract:

1. The Lua command body is captured into a `sol::protected_function`
   member of the wrapper lambda at `IRCommand.createCommand` /
   `IRCommand.bindPrefab` time.
2. The wrapper lambda is move-stored into
   `CommandManager::m_userCommands` as the `std::function<void()>`
   payload of a `CommandStruct<COMMAND_BUTTON>`.
3. The `sol::protected_function` holds a reference into the
   `sol::state` it was constructed from. That `sol::state` is owned
   by `LuaScript`, which is owned by `World`.
4. `CommandManager` is also owned by `World`.

**The teardown ordering is what matters.** Today, both managers are
owned by `World`:
- `CommandManager` is constructed first (input/command stack), then
  `LuaScript` (script stack).
- Destruction is reverse-order: `LuaScript` first (sol::state
  destroyed → all sol references invalidated), then `CommandManager`
  (`m_userCommands` destroyed → wrapper lambdas destroyed → already-
  invalidated `sol::protected_function`s destroyed).

That order is **safe**: destroying an already-invalidated
`sol::protected_function` does not call into the dead sol::state
— it just releases the C++-side reference count holder, which is a
plain `std::shared_ptr`-shaped object.

The risk is if `World` calls into Lua-defined commands during teardown
*after* `LuaScript` is destroyed. There is no such call path today
(commands fire only during `INPUT_KEY_MOUSE`, which is part of the
pipeline that stops running before `World`'s destructor body
proceeds). PR 2 must verify this by:

1. Adding a test (`test/script/lua_command_test.cpp`) that registers a
   Lua command, ticks the pipeline once, then tears down the World.
   No crash, no `std::terminate`.
2. Documenting the ordering rule in `engine/command/CLAUDE.md`'s
   "Gotchas" section.

**Hot reload of command bodies is out of scope for v1.** A creation
that wants to swap a Lua command's body re-registers it and uses the
new `CommandId`. There is no `IRCommand.replaceCommandBody` analogue
to `IRSystem.replaceSystemBody`. Adding one is straightforward (same
`shared_ptr<sol::protected_function>` reseat pattern) and is filed as
the follow-up when a creation needs it.

**`IRCommand.unbindAll()` is out of scope for v1.** The existing
`CommandManager` has no `m_userCommands.clear()` or per-id unbind
path; adding one needs care because the dispatch loop scans the vector
by index. If a creation needs runtime unbind, file a follow-up; the
common case (reload the demo by restarting the process) doesn't need
it.

## C++ referencing of Lua-defined commands

A C++ creation that wants to fire a Lua-defined command does:

```cpp
// In Lua bindings: capture the id returned by IRCommand.createCommand
// into a C++-accessible global (e.g. via a sol::table the C++ reads
// back). Or, register the Lua command from C++ by passing a sol::function
// into the C++ overload that takes Function (works because std::function
// type-erases sol::protected_function the same way it does any callable).

// Then fire imperatively:
IRCommand::fire(myLuaCommandId);
```

The `CommandId` is the bridge. It's stable for the lifetime of the
`CommandManager` (it's a vector index that never gets removed). A C++
creation reading the id back from Lua can call `IRCommand::fire(id)`
from any C++ code path — a system tick, a callback, an event handler.

**There is no "Lua commands as systems" path.** Commands and systems
have different shapes (commands are per-event callbacks; systems are
per-archetype iterators). The right move when a creation thinks it
wants to "compose Lua commands in a pipeline" is:

- If the work is per-entity → use `IRSystem.registerSystem` and put it
  in `IRSystem.registerPipeline`.
- If the work is per-event (e.g. "on this key, do this once") → use
  `IRCommand.createCommand` and bind it to an input trigger; or use
  `IRCommand.fire` from a system body to trigger it from non-input
  code.

A command that needs both shapes is two abstractions — a system that
does the per-entity work, plus a command that decides when to invoke
it. The boundary stays clean.

## Out of scope

These are deliberate non-goals for T-193 PR 2. Each is straightforward
to land later when a creation needs it.

- **Lua-defined `Command<NAME>::create()` specialization.** A
  Lua-defined command does not enter the `CommandNames` enum — it's
  identified by runtime `CommandId` only. Adding new prefab commands
  is still a C++-side change.
- **`IRCommand.replaceCommandBody(id, fn)`** — hot reload of Lua
  command bodies. File a follow-up if a creation needs it; the
  implementation mirrors `IRSystem.replaceSystemBody`.
- **`IRCommand.unbind(id)` / `IRCommand.unbindAll()`** — runtime
  removal. Needs care in `CommandManager` because dispatch is by
  vector index.
- **Modifier composition helper (`IRInput.modMask(...)`)** — use
  LuaJIT `bit.bor` directly.
- **Codegen of `IRCommand.CommandName` from `CommandNames`** — manual
  table is fine for ~35 entries; reconsider via the T-196 codegen
  research if `CommandNames` grows.
- **Lua API for `IRInput::checkKeyMouseButton` / `checkGamepadButton`
  query helpers.** Useful for systems that want to poll input state
  outside the command framework, but distinct from "bind commands."
  File a follow-up.
- **MIDI command bindings (`registerMidiNoteCommand` /
  `registerMidiCCCommand`)** — different parameter shape (the command
  body takes `(note, velocity)` or `(ccValue)` rather than no
  arguments). The `IRInput.InputType.MIDI_*` enum values are exposed
  so a follow-up Lua surface can build on top.
- **Lua-defined gamepad axis-to-value bindings** — gamepad axes are
  continuous, not button-status events. They are not currently routed
  through the command system from any path (C++ or Lua); they're
  polled per-frame via `IRInput::getGamepadAxis`. A separate Lua
  binding for the polling helper is out of scope here.

## Acceptance for PR 2

PR 2 lands when **all** of these check:

1. `engine/script/include/irreden/script/lua_command_bindings.hpp`
   exists and exposes the API described in
   [Public Lua API](#public-lua-api).
2. `LuaScript::bindLuaCommands()` wires the three bind helpers and is
   idempotent.
3. `engine/command/include/irreden/ir_command.hpp` exposes
   `IRCommand::fire(CommandId)` and `IRCommand::fireByName(CommandNames)`.
4. `engine/command/include/irreden/command/command_manager.hpp` exposes
   `CommandManager::fireUserCommand(CommandId)`.
5. The 18-call `initCommands()` block at
   [`creations/demos/default/main_lua.cpp:107-200`](../../creations/demos/default/main_lua.cpp)
   is moved to Lua. The C++ entry point either deletes the function or
   leaves it empty with a comment pointing at the Lua call site. Demo
   behaves identically (zoom, camera move, screenshot, F8/F9/F10 still
   work).
6. Existing C++ command path (`template <> struct
   IRCommand::Command<NAME>`) keeps working with no behavior change.
   No prefab command is deleted by this PR.
7. `test/script/lua_command_test.cpp` covers:
   - Bind prefab command from Lua → input event fires it (verified by
     ECS state change or a counter component).
   - Lua-defined command with closure body → input event invokes it.
   - `IRCommand.fire(id)` from a system body invokes a Lua command.
   - `IRCommand.fireByName(CN.SCREENSHOT)` triggers
     `IRVideo::requestScreenshot` (assert via the video manager's
     pending-screenshot flag).
   - Error in a Lua command body is caught, logged, and does not
     abort the dispatch loop or crash the world.
   - World teardown after registering a Lua command is clean
     (lifetime contract).
8. `fleet-build --target IrredenEngineTest` clean on linux-debug.
9. `fleet-build --target IRDefault` clean and the demo runs
   end-to-end with the Lua-driven command block.
10. `engine/command/CLAUDE.md` updated: add a "## Lua-defined commands"
    section pointing at this design doc, and a "Gotchas" entry on the
    Lua lifetime ordering rule.
11. `engine/script/CLAUDE.md` updated: extend the existing surface
    description with a `## Commands and input (IRCommand.*)` section
    next to the modifier framework section.

Out of scope for PR 2: porting any other demo. The `default` demo is
the migration validation; other demos migrate at their authors' pace.
