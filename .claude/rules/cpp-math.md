---
paths:
  - "engine/**/*.{hpp,cpp,h,cc}"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

# Math primitives go through IRMath, never glm:: or std::

Rule, with zero exceptions outside `engine/math/`:

> **Never** call `glm::*`, `std::sin`, `std::cos`, `std::tan`, `std::sqrt`, `std::abs`, `std::min`, `std::max`, `std::clamp`, `std::floor`, `std::ceil`, `std::round`, `std::pow`, `std::atan2`, `std::asin`, or `std::acos` from C++ files outside `engine/math/`.

The wrapper layer in [`engine/math/include/irreden/`](../../engine/math/include/irreden/) owns everything. Two reasons:

1. **One place to swap implementations.** If we ever switch from glm to a faster custom path (or add a SIMD variant), it changes in `engine/math/` and every caller picks it up.
2. **One place to encode CPU↔GPU consistency.** `IRMath::roundHalfUp` mirrors the GLSL/Metal `roundHalfUp` so half-integer positions classify the same on both sides. `glm::round` does not. Without the wrapper layer, this kind of consistency rule has to be re-asserted at every call site.

## What to use instead

| Don't write | Write |
|-------------|-------|
| `glm::vec3` / `glm::ivec3` / `glm::mat4` | `IRMath::vec3` / `IRMath::ivec3` / `IRMath::mat4` |
| `glm::min(a, b)` / `glm::max(a, b)` | `IRMath::min(a, b)` / `IRMath::max(a, b)` |
| `glm::clamp(v, lo, hi)` | `IRMath::clamp(v, lo, hi)` |
| `glm::length(v)` / `glm::normalize(v)` / `glm::dot(a, b)` | `IRMath::length(v)` / `IRMath::normalize(v)` / `IRMath::dot(a, b)` |
| `glm::sin(x)` / `glm::cos(x)` / `glm::sqrt(x)` | `IRMath::sin(x)` / `IRMath::cos(x)` / `IRMath::sqrt(x)` |
| `glm::pi<float>()` / `glm::half_pi<float>()` / `glm::two_pi<float>()` | `IRMath::kPi` / `IRMath::kHalfPi` / `IRMath::kTwoPi` |
| `std::min(a, b)` / `std::max(a, b)` / `std::clamp(...)` | `IRMath::min` / `IRMath::max` / `IRMath::clamp` |
| `std::sin(x)` / `std::cos(x)` / `std::abs(x)` | `IRMath::sin(x)` / `IRMath::cos(x)` / `IRMath::abs(x)` |
| `std::cbrt(x)` | `IRMath::cbrt(x)` |
| `std::fmod(x, p)` + `if (v < 0) v += p`, or `while` ±2π wrap loops | `IRMath::wrapToRange(x, p)` / `IRMath::wrapAngleTwoPi(a)` / `IRMath::wrapAnglePi(a)` |

## Iso projection: never inline the equations

Canonical equations and named helpers: `engine/math/CLAUDE.md §"Isometric projection — the equations"`. Always call the helpers; never inline.

## Binary I/O of math types

Serialization helpers for `IRMath::vec*`, `IRMath::Color`, `IRMath::quat`, and other math types belong in `engine/math/` (or alongside `BinaryWriter` / `BinaryReader` in `engine/asset/`) — never inline in a format-specific `.cpp`. Each binary asset format (`.vxs`, `.rig`, future `.prefab.lua`) is a consumer; the helpers are shared infrastructure.

The "every format author writes their own" failure mode produces near-duplicate helpers under different names — historically `writeVec3` / `encodeVec3` defined twice across `voxel_set_format.cpp` and `rig_format.cpp`, with different signatures and byte layouts. Centralizing the helpers keeps the byte layout consistent across formats and concentrates the round-trip tests in one place.

If the helper you need doesn't exist yet:

1. Add it to `engine/asset/include/irreden/asset/math_binary_io.hpp` under `namespace IRMath::BinaryIO` — the helpers live in `engine/asset/` rather than `engine/math/` because inline implementations that call `BinaryWriter` / `BinaryReader` methods need the full type definition, and `engine/math/` must not depend on `engine/asset/`.
2. Or, if the type owns its own representation (`Color` knows how to pack-RGBA), put the serializer on the type as a static method (`Color::toPackedRGBA()` / `Color::fromPackedRGBA(uint32_t)`).
3. Then call it from the format code. Standardize naming on `read` / `write` to match `BinaryReader::readU32` / `BinaryWriter::writeU32`; do not introduce `encode` / `decode` / `pack` / `unpack` aliases.

## When the wrapper doesn't exist yet

If you need a primitive `IRMath` doesn't expose, **add the wrapper to `engine/math/` first**, then call it. Don't reach for `glm::` "just for now" — that's the path that produced the 164 violations this rule exists to clean up.

The math library may itself wrap `glm::*` / `std::*` internally — that is the **only** place those names should appear.

## Allowlist (do NOT flag these)

- Anything in `engine/math/**` itself.
- The graphics-backend interop layer at `engine/render/include/irreden/render/backend/**` — when wiring an actual `glm` value into a `glDrawElements`-shaped API, raw glm types are the surface.
- Shader source: `*.glsl`, `*.metal`. These have their own native math; the rule is about C++ files.
- Standalone tools under `tools/**` that do not link the engine library (`jitter_probe`, `img_diff`): IRMath lives in `engine/math/` and is genuinely unavailable there, so `std::`/`<cmath>` math is correct. (Also outside this rule's `paths:` scope — don't raise the nit from prose alone.)

## Live deviations (queue-manager-owned)

The current list of files still calling `glm::*` outside the allowlist lives in `.fleet/status/glm-deviations.md` (or will, once it's introduced — track via a follow-up task). Don't add new violations; do migrate when you're already touching one of the deviation files.
