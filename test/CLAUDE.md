# Unit Tests

GoogleTest-based test suite for the Irreden Engine. All tests live in
the centralized `test/` tree (not co-located with source) and compile
into a single `IrredenEngineTest` binary that links against the full
`IrredenEngine` static library.

---

## Why centralized, not co-located

The engine modules are tightly coupled static libraries — ECS tests
need entity + math + profile wired up together, system tests need the
full archetype runtime, etc. A per-module test binary would duplicate
all that linkage. The centralized tree gives us one binary, one
FetchContent pull of gtest, and a directory structure that mirrors the
engine modules for discoverability.

---

## Directory layout

Mirror the engine module path under `test/`:

```
test/
  CMakeLists.txt          # single IrredenEngineTest target
  CLAUDE.md               # this file
  entity/                 # tests for engine/entity/
    entity_manager_test.cpp
    archetype_test.cpp
    archetype_graph_test.cpp
    archetype_node_test.cpp
  math/                   # tests for engine/math/
    physics_test.cpp
    ir_math_test.cpp
  system/                 # tests for engine/system/
    system_manager_test.cpp
  render/                 # tests for engine/render/
  world/                  # tests for engine/world/
  ...
```

Create the subdirectory when adding the first test file for a module.

**Migration note:** `entity_manager_test.cpp` currently lives at
`test/ecs/` (legacy path). The first entity-test task should move it to
`test/entity/` and update `test/CMakeLists.txt` accordingly.

---

## File naming

- One test file per source file: `<source_basename>_test.cpp`
- Example: `engine/entity/src/archetype_graph.cpp` is tested by
  `test/entity/archetype_graph_test.cpp`
- For header-only code (e.g. prefabs), name the test after the header:
  `test/prefabs/c_position_test.cpp` for a component header

---

## Registration

Every new `_test.cpp` **must** be added to the `add_executable` source
list in `test/CMakeLists.txt`. `gtest_discover_tests()` handles CTest
registration automatically — no manual `add_test()` calls needed.

---

## Test naming

| Element             | Convention                          | Example                                          |
|---------------------|-------------------------------------|--------------------------------------------------|
| Test suite (fixture)| `PascalCase`, module prefix allowed | `EntityManagerTest`, `ArchetypeGraphTest`         |
| Test suite (plain)  | `PascalCase`                        | `PhysicsTest`, `IsoMathTest`                      |
| Test name           | `PascalCase`, descriptive verb      | `CreateEntityReturnsNonNull`                      |
| Fixture class       | Same as test suite name             | `class EntityManagerTest : public testing::Test`  |

Use `TEST_F` (fixture) when tests share setup — typically anything that
needs ECS runtime state (entity manager, archetypes). Use plain `TEST`
for pure functions (math helpers, coordinate transforms).

---

## Test components and helpers

Test-only component types use the `Test` prefix — `TestMarker`,
`TestPayload`, `TestRemovable`. Do **not** use the engine's `C_` prefix
for test components (they aren't real components registered in the
engine).

Keep test helpers (custom matchers, shared fixtures) in a `test/support/`
directory if they're used by more than one test file. For single-file
helpers, define them in an anonymous namespace at the top of the file.

---

## Floating-point comparisons

Use `EXPECT_NEAR(actual, expected, tolerance)` with a file-scoped
tolerance constant:

```cpp
namespace {
constexpr float kTolerance = 1e-5f;
} // namespace
```

Do not use `EXPECT_FLOAT_EQ` — it uses ULP-based comparison that can
be surprising for computed values.

---

## Assertion guidelines

- `EXPECT_*` for checks that should continue on failure (most cases)
- `ASSERT_*` only when the test cannot meaningfully continue after
  failure (e.g. a pointer is null and the next line dereferences it)
- Prefer the most specific assertion: `EXPECT_EQ` over `EXPECT_TRUE(a == b)`,
  `EXPECT_NEAR` over manual tolerance checks

---

## Structure within a test file

```cpp
#include <gtest/gtest.h>
#include <irreden/the_header_under_test.hpp>

namespace {

// Test-only types (if needed)
struct TestMarker {};

// Tolerance constants (if needed)
constexpr float kTolerance = 1e-5f;

// Fixture (if needed)
class FooTest : public testing::Test {
  protected:
    FooTest() : m_thing{} {}
    SomeType m_thing;
};

// Tests — group by function/behavior, most basic first
TEST_F(FooTest, BasicOperation) { ... }
TEST_F(FooTest, EdgeCase) { ... }
TEST_F(FooTest, ErrorCondition) { ... }

} // namespace
```

Wrap everything in an anonymous namespace. This prevents ODR collisions
between test files that define identically-named helpers.

---

## Building and running

```bash
# Build
cmake --build build --target IrredenEngineTest -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Run all tests
ctest --test-dir build --output-on-failure

# Run tests matching a pattern
ctest --test-dir build --tests-regex "EntityManager"

# Run the binary directly (verbose gtest output)
./build/IrredenEngineTest --gtest_filter="EntityManagerTest.*"
```

---

## GoogleTest version

Pinned to a release tag in `test/CMakeLists.txt` (not `main`). Update
the tag deliberately, not by accident. If you need a newer gtest
feature, update the `GIT_TAG` and note the reason in the commit message.

---

## What to test (guidance for task authors)

**Good targets for unit tests:**
- Pure functions (math helpers, coordinate transforms, physics formulas)
- Entity manager operations (create, destroy, component add/remove/get,
  archetype migration)
- Archetype graph traversal and matching
- System creation and tick dispatch (with mock components)
- Deferred structural changes (the flush/apply cycle)
- World/chunk management

**Not good targets for unit tests (use integration/visual tests):**
- Render pipeline output (requires GPU context)
- Audio playback
- Window/input handling (requires OS event loop)
- Shader compilation (requires graphics backend)

When a test uncovers a real bug in the code under test, **stop and
requeue as `[opus]`** with a bug report rather than fixing the bug
inline in a test PR. Test PRs should test existing behavior, not
change it.
