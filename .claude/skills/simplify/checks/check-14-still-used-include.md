# Check 14 — removal of a still-used standard-library include

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff removes an
`#include <...>` line.

A refactor that removes the last std-symbol use from the *edited region*
can drop an include that untouched code in the same file still needs —
compiling today only through a transitive include. For each `-#include
<header>` line, grep the file's surviving lines against:

- `<cstdint>` → `std::u?int(8|16|32|64)_t`, `std::u?intptr_t`
- `<cstddef>` → `std::size_t`, `std::ptrdiff_t`, `std::byte`
- `<cstring>` → `std::mem\w+`, `std::str\w+`
- `<vector>` / `<array>` / `<string>` / `<optional>` / `<memory>` → the
  type names (`std::vector`, `std::array`, `std::string`,
  `std::optional`, `std::unique_ptr` / `std::shared_ptr` / `std::make_*`)

A match means the include is still load-bearing. Auto-fix: restore it.
