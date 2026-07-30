# Check 14 — removal of a still-used standard-library include

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff removes an `#include <...>` line.

A refactor that removes the last std-symbol use from the *edited region*
of a file can drop the `#include` while untouched code in the same file
still uses it — compiling today only via a transitive include path, i.e. a
latent break (PR #2509 dropped `<cstdint>` with `std::int32_t` live at
four untouched lines; two review passes were spent on it). For each
`-#include <header>` line in the diff, grep the file's surviving lines
against a finite header→symbol map:

- `<cstdint>` → `std::u?int(8|16|32|64)_t`, `std::u?intptr_t`
- `<cstddef>` → `std::size_t`, `std::ptrdiff_t`, `std::byte`
- `<cstring>` → `std::mem\w+`, `std::str\w+`
- `<vector>` / `<array>` / `<string>` / `<optional>` / `<memory>` → the
  obvious type names (`std::vector`, `std::array`, `std::string`,
  `std::optional`, `std::unique_ptr` / `std::shared_ptr` / `std::make_*`)

A match means the include is still load-bearing. Auto-fix: restore it.
(#2517)
