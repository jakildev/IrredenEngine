---
paths:
  - "engine/**/*.{hpp,cpp,h,cc}"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

> Sweep with `fleet-rules-sweep`, never `rg`/`Grep` rooted at `creations/`
> — see [`README.md`](README.md).

# Math primitives go through IRMath, never glm:: or std::

Rule, with zero exceptions outside `engine/math/`:

> **Never** call `glm::*`, `std::sin`, `std::cos`, `std::tan`, `std::sqrt`,
> `std::abs`, `std::min`, `std::max`, `std::clamp`, `std::floor`,
> `std::ceil`, `std::round`, `std::pow`, `std::atan2`, `std::asin`, or
> `std::acos` from C++ files outside `engine/math/`.

The wrapper layer in `engine/math/include/irreden/` is the one place to swap
implementations and the one place CPU↔GPU consistency is encoded
(`IRMath::roundHalfUp` mirrors the shader `roundHalfUp`; `glm::round` does
not). A `round` on a **position → cell** assignment is `IRMath::roundHalfUp`
/ `roundVec3HalfUp`, never `round` — round-half-away-from-zero disagrees with
the GPU at negative half-integers.

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
| `std::pow(b, e)` / `std::log2(x)` | `IRMath::pow(b, e)` / `IRMath::log2(x)` |
| `std::pow(2.0f, std::round(std::log2(x)))` | `IRMath::snapToPowerOfTwo(x)` |
| `std::fmod(x, p)` + `if (v < 0) v += p`, or `while` ±2π wrap loops | `IRMath::wrapToRange(x, p)` / `IRMath::wrapAngleTwoPi(a)` / `IRMath::wrapAnglePi(a)` |

If the wrapper you need doesn't exist, add it to `engine/math/` first, then
call it — never `glm::` "just for now". `IRMath::clamp` / `max` / `min` take
one type parameter: spell mixed vector/scalar bounds as vectors
(`clamp(v, vec3(0.0f), vec3(1.0f))`). The math library itself is the only
place `glm::*` / `std::*` math names appear.

## Iso projection: never inline the equations

Helpers and equations: `engine/math/CLAUDE.md` §"Isometric projection — the equations".

## Binary I/O of math types

Serializers for `IRMath::vec*`, `IRMath::Color`, `IRMath::quat`, … are shared
infrastructure, never inline in a format-specific `.cpp`. Add a missing one
to `engine/asset/include/irreden/asset/math_binary_io.hpp` under
`namespace IRMath::BinaryIO` (the inline bodies need the full
`BinaryWriter` / `BinaryReader` types, and `engine/math/` must not depend on
`engine/asset/`), or as a static method on a type that owns its
representation (`Color::toPackedRGBA()` / `Color::fromPackedRGBA(uint32_t)`).
Name them `read` / `write` to match `BinaryReader::readU32`; no `encode` /
`decode` / `pack` / `unpack` aliases.

## Allowlist (do NOT flag these)

- `engine/math/**`.
- The graphics-backend interop layer `engine/render/include/irreden/render/backend/**`.
- Shader source (`*.glsl`, `*.metal`).
- Standalone tools under `tools/**` that do not link the engine library
  (also outside this rule's `paths:`).
- `engine/profile/**` — does not link `IrredenEngineMath`; drop this
  carve-out if it ever gains a math dependency.

## Detection

Tree-wide, through `fleet-rules-sweep` (exit **1** = clean pass, **0** =
violations, **2** = scope resolved to zero files). `rg` cannot cover this
scope at any root or glob: three tracked files under `creations/` sit in
re-ignored directories (`creations/bazel_test/`,
`creations/editors/font_maker/`) that ripgrep never walks, and `--no-ignore`
would pull in the private `creations/game` clone.

```
fleet-rules-sweep \
  --glob 'engine/**/*.{hpp,cpp,h,cc}' --glob 'creations/**/*.{hpp,cpp,h,cc}' \
  --glob '!engine/math/**' --glob '!engine/profile/**' \
  --glob '!engine/render/include/irreden/render/backend/**' --glob '!tools/**' \
  --pattern '^(?!\s*(//|\*)).*(\bglm::\w+|\bstd::(sin|cos|tan|sqrt|abs|min|max|clamp|floor|ceil|round|pow|log2|atan2|asin|acos|cbrt|fmod)\b)'
```

The `simplify` math check and `review-pr` read a diff; this form measures
the standing population.

## Live deviations

**Zero.** This register is the whole record — there is no external status
file. The Detection sweep exits 1 over the full scope.
