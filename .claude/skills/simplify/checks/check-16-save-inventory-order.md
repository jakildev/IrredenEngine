# Check 16 — `save_component_inventory.hpp` include order

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `engine/world/include/irreden/world/save_component_inventory.hpp`.

`.clang-format` sets `SortIncludes: Never` precisely so this hand-audited
inventory keeps its grouping — which means nothing mechanical will ever
fix or even notice a misplaced entry, on a ~135-line include block that
`engine/world/CLAUDE.md` §"New-component contract" makes every new engine
component edit (#2624 slotted `settings_registry.hpp` mid-`components/`
run; a reviewer caught it). When the diff touches
`engine/world/include/irreden/world/save_component_inventory.hpp`, check
each added `#include` line in the component block (everything below the
file's own leading `save_trait.hpp` include) sorts alphabetically by full
path against both neighbors. Scoped to this one file by design — other
headers group by module intentionally. Auto-fix: move the line to its
slot. (#2636)
