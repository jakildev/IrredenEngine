# test/ — the engine's GoogleTest suite

Every test in this tree compiles into a **single** `IrredenEngineTest`
executable that links the whole `IrredenEngine` library plus `gtest_main`.
There is no per-module test target: a test can reach any engine header, and
`gtest_discover_tests` registers each test individually with CTest.

## New test files must be registered by hand

`test/CMakeLists.txt` lists every source explicitly in the
`add_executable(IrredenEngineTest ...)` block — there is no glob. **A new
`.cpp` that isn't added to that list silently does not build**: no error, no
warning, and your tests simply never run. This is the most common authoring
miss in this tree; it looks exactly like a passing suite.

Add the file to the list under the group matching its directory.

## Where a test goes

Tests live in `test/<area>/`, where `<area>` mirrors the engine module under
test (`engine/math/` → `test/math/`, `engine/script/` → `test/script/`, and
so on). New engine functionality lands with its tests in the matching
subdirectory — reach for `Glob` to see the current set rather than trusting a
list here.

## Build and run

```bash
fleet-build --target IrredenEngineTest
fleet-run IrredenEngineTest                                  # whole suite
fleet-run IrredenEngineTest --gtest_filter='SystemCadenceTest.*'   # one suite
```

`--gtest_filter` takes a `Suite.Test` glob, so `--gtest_filter='*Cadence*'`
narrows by substring across suites. Prefer a filtered run while iterating —
the full suite links and boots every engine subsystem.

## Coverage is per new public surface, not per PR

Author coverage **per new public surface the diff adds**, not per PR. "The PR
has a test" is not the bar: a change that covers one surface while shipping
others bare is expected back with `needs-fix` on the uncovered ones (the
`Tests / build` checklist in `.claude/skills/review-pr/SKILL.md` is where this
is enforced at review). Per surface, that means:

- a new Lua binding → a `test/script/lua_*_test.cpp` exercising the sol2 seam;
- a facility with two registration paths → a test through **each** path;
- other new public surface (`ir_*.hpp` API, component, system, serialized
  format) → a test in the matching `test/<area>/`.

The waiver is human-explicit — someone literally says "no tests". Never infer
it.

**Worked example (#2425, per-system cadence).** The change shipped 13 solid
`SystemManager` tests, and still left two surfaces uncovered: the
`IRSystem.*Cadence*` Lua bindings, and the `kCadence` / `kCadenceOffset`
spec-member detection path on `System<N>`. Both are public, both are how
downstream code actually consumes the feature, and "a new test exists" hid
them. Count surfaces, not tests.

## Lua seam tests

The C++ API and the Lua binding are **separate surfaces** — a test that drives
`SystemManager` directly proves nothing about the sol2 seam in front of it.
Cover the binding by driving Lua:

```cpp
class MyLuaTest : public testing::Test {
  protected:
    MyLuaTest() { m_lua.bindLuaDrivenEcs(); }

    IRScript::LuaScript m_lua;          // declared FIRST → destroyed LAST
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(MyLuaTest, BindingIsReachable) {
    auto result = m_lua.lua().safe_script(R"lua(
        return IRSystem.getSystemCadence(0)
    )lua");
    ASSERT_TRUE(result.valid());
}
```

**Member order is load-bearing.** `LuaScript` must be declared before the
managers so it is destroyed *last*: `sol::function` references captured in
dynamic-system bodies call `lua_unref` during manager teardown, which requires
a still-open `lua_State`. Get the order backwards and you get a crash at
fixture teardown, not at the assertion.

Scripts run inline via `safe_script` — assert on `result.valid()`, since a Lua
error otherwise surfaces as a confusing value rather than a failed test. The
`.lua` files in `test/script/` are **not** the general pattern; they are
codegen fixtures (see below).

## Codegen fixtures

`test/script/*.lua` fixtures feed the build-time codegen path and are wired
through `irreden_lua_codegen(IrredenEngineTest SOURCES ... DEFAULT_MODE
CODEGEN)` blocks in `CMakeLists.txt`. Two constraints there are deliberate:

- **`DEFAULT_MODE CODEGEN` is pinned explicitly**, overriding the
  `IR_LUA_ECS_DEFAULT_MODE` cache variable. These tests exist to verify the
  typed-emission path; if the global flag flipped them to EVAL they would
  silently pass while testing nothing.
- **Fixture paths reach the test through compile definitions**, never a
  relative path. The engine `chdir`s to the executable directory at boot but
  the test binary does not, so a relative fixture path resolves differently
  under CTest than under a direct run.

See [`engine/script/CLAUDE.md`](../engine/script/CLAUDE.md) for the binding
surface itself and the CODEGEN-vs-EVAL split.
