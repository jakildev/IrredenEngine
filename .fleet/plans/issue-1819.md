# Plan: Minimal high-score + settings persistence

- **Issue:** #1819
- **Model:** sonnet
- **Date:** 2026-06-15

## Scope

A minimal, Lua-reachable key/value persistence surface for an arcade build:
save a set of named values (scalars, strings, and a high-score list) to a
per-user data directory and read them back across launches. **Not** the
ECS world-snapshot (#199 / epic #667) — that walks the archetype graph and
lives in `engine/world/`. This is a flat KV file built on the existing
`engine/asset/` binary primitives.

Deliverable surfaces:
1. A platform-appropriate per-user data dir resolver (`engine/utility/`).
2. A flat `.irkv` binary key/value format + in-memory `KeyValueStore`
   (`engine/asset/`), built on `BinaryWriter`/`BinaryReader` + chunk-table
   helpers — **no raw `fopen`/`fwrite`** (asset module hard rule).
3. Lua bindings (`IRSave` table) so gameplay can `save`/`load`/`set`/`get`.
4. Round-trip + corruption-recovery test under `test/asset/`.

## Approach (single, committed)

### 1. Per-user data dir helper — `engine/utility/`
Add `std::string IRUtility::userDataDir(const std::string &appName)` to
`engine/utility/include/irreden/utility/path_utils.hpp` (decl) +
`engine/utility/src/ir_utility.cpp` (impl; the module already includes
`<filesystem>`). Resolution, by platform (env-var first, HOME fallback):
- **Linux:** `${XDG_DATA_HOME:-$HOME/.local/share}/<appName>`
- **macOS:** `$HOME/Library/Application Support/<appName>`
- **Windows:** `%APPDATA%\<appName>` (fallback `%USERPROFILE%\<appName>`)

Use `#if defined(_WIN32) / __APPLE__ / else` + `std::getenv`. The helper is
**pure** (resolves the path, does NOT mkdir) — matching the existing
`joinPath` "no directory creation" contract. `appName` lowercased to
`irreden` keeps it consistent with the engine's `$XDG_CACHE_HOME/irreden`
tooling convention; callers pass their own product subdir if they want one.
Directory creation happens in the store's save path (step 2) via
`std::filesystem::create_directories`. Keep this narrow — it is squarely a
path helper, not a kitchen-sink addition (utility CLAUDE caveat).

### 2. `.irkv` format + `KeyValueStore` — `engine/asset/`
New files:
- `engine/asset/include/irreden/asset/key_value_store.hpp`
- `engine/asset/src/key_value_store.cpp`
- expose entry points via `engine/asset/include/irreden/ir_asset.hpp`.

**In-memory model.** `IRAsset::KeyValueStore` wraps
`std::unordered_map<std::string, Value>` where `Value` is a tagged variant
over the minimal sufficient set:
- `NUMBER` → `double` (LuaJIT numbers are all doubles; exact integers to
  2^53 cover high scores + settings)
- `BOOL` → `bool`
- `STRING` → `std::string`
- `LIST` → `std::vector<ListElem>`, where `ListElem` is itself
  `NUMBER|BOOL|STRING` (no nested lists in v1) — covers a high-score table
  (list of numbers) or a name list (list of strings) in one mechanism.

API: `set(key, Value)`, `get(key) -> const Value*` (null if absent),
typed convenience `getNumber/getString/getBool(key, fallback)`, `has`,
`remove`, `clear`, `keys()`.

**On-disk format** (obeys the 7 Save Format Extensibility Rules):
- Header via `chunk_header.hpp` `writeChunked(...)`: magic `IRKV`,
  `version = 1`, one chunk `KVPR`.
- `KVPR` body: `writeVarUInt(count)`, then per entry:
  `writeString(key)`, `writeU8(valueTag)`, then the tag's payload
  (`writeF64` / `writeU8(bool)` / `writeString` / for `LIST`:
  `writeVarUInt(n)` + per-elem `writeU8(elemTag)` + payload).
- Save: `FileBinaryWriter` → `writeChunked`. `loadKeyValueStore(path)`:
  `FileBinaryReader` + `readChunks(reader, "IRKV", 1)`; decode `KVPR`.
- **Free-function entry points** (mirror `saveRig`/`loadRig` shape):
  `bool saveKeyValueStore(const std::string &path, const KeyValueStore&)`
  (calls `std::filesystem::create_directories` on the parent dir first)
  and `Result<KeyValueStore> loadKeyValueStore(const std::string &path)`.
  Reuse the `Result<T>` / `BinaryIOError` wrappers already in
  `binary_io.hpp`. Unknown value tag, truncation, bad magic, or
  `VersionTooNew` → recoverable error / empty store (Rule #5), never a
  crash. Missing file → `OpenFailed` Result, caller treats as "no save →
  defaults."
- **No JSON sidecar needed** (settings/scores aren't designer-diffed); if
  desired later, add via `json_sidecar.hpp` as a follow-up — do NOT block
  v1 on it.

### 3. Lua bindings — `IRSave` table
New `engine/script/include/irreden/script/lua_persistence_bindings.hpp`
with `inline void IRScript::detail::bindPersistenceApi(LuaScript &script)`,
mirroring `lua_sim_bindings.hpp`'s `bindSimApi` shape. Register it from
`LuaScript::bindLuaDrivenEcs()` in `engine/script/src/lua_script.cpp`
(add the `#include` next to the other `lua_*_bindings.hpp` includes and a
`detail::bindPersistenceApi(*this);` call alongside the existing
`detail::bind*` calls — same site `bindRenderGlue`/#1615 uses).

The binding owns a `std::unordered_map<std::string, KeyValueStore>` keyed
by **store basename** (e.g. `"highscores"`, `"settings"`), held in a
`std::shared_ptr` captured by the binding lambdas (process-registry
pattern, like `IRPrefab::Prefab`). Store files live at
`userDataDir("irreden") / <basename>.irkv`.

`IRSave` surface (store basename is the first arg; values cross the
boundary by Lua type inspection — number/string/bool/table — NOT by an
enum spelled in Lua, so the `cpp-lua-enums` rule does not apply here):
```lua
IRSave.load("highscores")            -- read file into the in-proc store;
                                     -- missing/corrupt -> empty. returns bool (loaded vs fresh)
IRSave.save("highscores")            -- write in-proc store to disk; returns bool ok
IRSave.set("highscores", "top1", 5000)   -- number | string | bool | array-table
IRSave.get("highscores", "top1", 0)      -- value or the default arg
IRSave.has("highscores", "top1")
IRSave.remove("highscores", "top1")
IRSave.clear("highscores")
```
Lua → `Value`: a Lua **array** table (1..n contiguous) becomes a `LIST`;
a non-array table is a binding error (clear diagnostic, not a silent
no-op). `get` returns a Lua number/string/bool, or a fresh array table
for a `LIST`. Use `IRScript`'s existing helpers where they fit; numbers
go through `lua_Number`/`double`.

### 4. Test — `test/asset/key_value_store_test.cpp`
Register in `test/CMakeLists.txt` next to `asset/rig_format_test.cpp`.
Mirror `rig_format_test.cpp` structure. Cases: set→save→load round-trip
for each value type incl. a `LIST`; corrupt-magic file → recoverable
error + empty store; truncated-mid-chunk → recoverable; missing file →
`OpenFailed`/defaults; overwrite + remove + clear semantics. Write
fixtures to a temp dir (use the same temp-path approach the existing
asset tests use). A focused Lua-binding smoke test (`IRSave.set/get`
round-trip headless) may be added under `test/script/` mirroring
`lua_sim_test.cpp`, but is optional for v1 — the C++ round-trip test is
the gate.

## Affected files
- `engine/utility/include/irreden/utility/path_utils.hpp` — decl `userDataDir`.
- `engine/utility/src/ir_utility.cpp` — impl `userDataDir` (platform `#if`).
- `engine/asset/include/irreden/asset/key_value_store.hpp` — new: `Value`, `KeyValueStore`, save/load decls.
- `engine/asset/src/key_value_store.cpp` — new: format encode/decode via `chunk_header`/`binary_io`.
- `engine/asset/include/irreden/ir_asset.hpp` — re-expose the new entry points.
- `engine/asset/CMakeLists.txt` — add the new `.cpp`.
- `engine/script/include/irreden/script/lua_persistence_bindings.hpp` — new: `bindPersistenceApi` (`IRSave`).
- `engine/script/src/lua_script.cpp` — include + call from `bindLuaDrivenEcs()`.
- `test/asset/key_value_store_test.cpp` — new round-trip + corruption test.
- `test/CMakeLists.txt` — register the new test.
- `engine/asset/CLAUDE.md` — document the `.irkv` format header block (Extensibility Rule #7).

## Acceptance criteria
- A creation can `IRSave.set` a high-score list + a couple of settings from
  Lua, `IRSave.save`, and read them back after relaunch via `IRSave.load` +
  `IRSave.get`.
- The save file resolves under the per-user data dir (NOT the exe dir), so
  it survives a clean reinstall of the game bundle.
- Missing or corrupt save file degrades gracefully to defaults (empty store),
  no crash — covered by the test fixtures.
- New format has a header block in `key_value_store.hpp` + `asset/CLAUDE.md`
  per Extensibility Rule #7; all I/O routes through `BinaryWriter`/`Reader`.

## Gotchas
- **No raw `fopen`/`fwrite`** for the new format — asset-module hard rule;
  route through `FileBinaryWriter`/`FileBinaryReader`. Reads return
  `Result<T>`; check `.ok()` before consuming.
- **`joinPath` does not mkdir.** Create the user data dir in the save path
  with `std::filesystem::create_directories` before writing.
- **LuaJIT numbers are all doubles** (no integer subtype). Store numbers as
  `f64`; high scores are exact to 2^53. Don't add an int64 value type unless
  a real consumer needs > 2^53.
- **`cpp-lua-enums` does NOT apply** — values are typed by `sol::type`
  inspection (number/string/bool/table), not a Lua-spelled enum. Do not
  invent an `IRSave.Type.*` enum.
- **Extensibility Rule #5:** corrupt/truncated/version-too-new must be
  recoverable (empty store + diagnostic), never fatal. Add the corrupt +
  truncated fixtures.
- **Per-user dir, not exe dir** — writing next to the exe fails the
  "survives clean reinstall" acceptance. The whole point of `userDataDir`.
- **Process-registry lifetime:** the `IRSave` store map is captured in the
  binding (shared_ptr), so it's per-`World`/per-`LuaScript`. Don't hold Lua
  handles across `World` shutdown (script CLAUDE caveat).
- Scope discipline: one PR, ~10 files, all additive. If the platform
  data-dir resolution or the format proves subtler than this plan (e.g. a
  consumer needs nested lists / >2^53 ints / a sidecar), escalate per the
  worker step-8 ladder rather than expanding scope silently.

## Sibling / in-flight reconciliation
- #199 / epic #667 (ECS world snapshot) — explicitly out of scope and lives
  in `engine/world/`; this is the flat-KV alternative the jam needs.
- #1813 (audio substrate, miniaudio) — independent; no shared surface.
- #1815 (packaging) — the "survives clean reinstall" acceptance aligns with
  packaging but does not block; `userDataDir` is the contract between them.
- No open PR touches `engine/asset/` KV / `userDataDir` (open PRs #1742,
  #1833 are render/GUI). No `userDataDir` helper exists in C++ today
  (greps clean) — this is a fresh add, not a duplicate.

## One task or stack?
**One task.** Cohesive ~10-file additive change (format + dir helper +
bindings + test). No epic/stack needed. Suggested model **[sonnet]** — the
primitives (`BinaryWriter`/`Reader`, chunk table, `lua_sim_bindings`
pattern, `rig_format` test) all exist and the approach is fully specified
here; the only judgment calls (platform dir, value set) are decided above.
