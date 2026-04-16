# engine/prefabs/irreden/wip/ — experimental, not production-ready

Prefabs that are mid-design and not yet safe to include from production
code. Nothing under `wip/` is referenced by any umbrella `ir_*.hpp`
header.

## Contents

- `components/component_alarm.hpp` — `C_Alarm`. A single `int alarmTime_`
  countdown member. Placeholder for a "fire once after N ticks" mechanism.
  Not yet integrated with any system.

## Rules

- **Nothing in `wip/` should be included from engine or creation code.**
  These files exist as design sketches. If you find a `#include` pointing
  here outside of `wip/` itself, remove it.
- **Graduating a component out of `wip/`.** When a component is ready:
  1. Move it to the correct domain directory (`common/`, `update/`, etc.).
  2. Add a system or other consumer in the same PR.
  3. Add the file to the appropriate umbrella `ir_*.hpp`.
  4. Delete it from `wip/`.
- **Files here must still compile.** The `lint` and `format-check` CMake
  targets scan everything under `engine/` including `wip/`. Keep
  `wip/` files syntactically valid even if the design is incomplete.
