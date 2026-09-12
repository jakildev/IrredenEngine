# Check 16 — `save_component_inventory.hpp` include order

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff touches
`engine/world/include/irreden/world/save_component_inventory.hpp`.

`.clang-format` sets `SortIncludes: Never` so this hand-audited inventory
keeps its grouping, which means nothing mechanical notices a misplaced
entry in the include block `engine/world/CLAUDE.md` §"New-component
contract" makes every new component edit. Check each added `#include` in
the component block (everything below the leading `save_trait.hpp`
include) sorts alphabetically by full path against both neighbours.
Scoped to this one file — other headers group by module intentionally.
Auto-fix: move the line to its slot.
