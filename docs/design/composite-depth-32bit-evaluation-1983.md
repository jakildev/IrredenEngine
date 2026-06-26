# 32-bit shared composite depth — evaluation (#1983)

Investigation spike, follow-on of sub-epic #1884. Asks whether **widening the
shared composite depth to 32-bit** would push the encodable ceiling out far
enough to (a) retire the `±65535` `normalizeDistance` convention and (b) let the
*simple additive priority band* fit any practical world — letting us drop the
#1958 disjoint near-plane partition special-casing.

**This spike prescribes no code change.** It is the durable evidence base for
the recommendation in the final section: *keep the partition; do not widen now.*
The #1958 partition already shipped (PR #1974, merged 2026-06-24) and is
unconditional; nothing here blocks B/C/D/E.

All measurements are macOS / Metal (Apple Silicon), `IRPerfGrid --mode dense
--grid-size 64`, current `origin/master`. GL numbers are analytic where noted —
the spike host runs Metal only; no GL benchmark gates the recommendation because
the recommendation is "no change".

## 1. Where the `±65535` ceiling actually binds

The ceiling is the **`normalizeDistance` convention on the shared framebuffer
depth**, not a hardware limit. The gather composite stores

```
gl_FragDepth = normalizeDistance(enc)
normalizeDistance(d) = (d − kMin) / (kMax − kMin)     // kMin = −65535, kMax = +65535
                                                       // ir_constants.hpp:47,51
enc = isoDepth · effSub · 4 + face                     // kDepthEncodeShift = 4; effSub cap 16
```

(`f_trixel_to_framebuffer.glsl:60-62,105`; `ir_render_types.hpp:200`.) More
negative `enc` ⇒ nearer (GL_LESS); `normalizeDistance` maps `−65535 → 0.0`
(nearest) and `+65535 → 1.0` (farthest).

So `enc` ranges over **131,071 distinct integer codes** with `[0,1]` spacing
`1/131071 ≈ 7.63e-6`. Two facts follow:

- **The depth *attachment* is not the binding constraint.** The attachment is
  `TextureFormat::DEPTH24_STENCIL8` (`ir_render_enums.hpp:30`):
  - **OpenGL** → `GL_DEPTH24_STENCIL8` (`opengl_types.hpp:76-77`): 24-bit unorm,
    **16.7M uniform codes**. Each `enc` code spans 128 attachment codes — every
    code resolves exactly, with vast headroom.
  - **Metal** → `MTL::PixelFormatDepth32Float_Stencil8`
    (`metal_texture.cpp:24-25`): 32-bit float depth. Apple-Silicon GPUs do not
    support a 24-bit depth/stencil format, so the engine is **already forced to
    float32 depth on Metal**. Float ulp near typical depth values is
    `≈ 6e-8 … 1.2e-7`, so the `7.63e-6` `enc` spacing is **~64–128 ulps** even at
    the far plane — every code, including the face/slot low bits, resolves
    exactly *at the current range*.

  Both backends resolve all 131,071 codes exactly today. The `±65535` is purely
  the **encodable dynamic range of the convention**, not attachment precision.

- **The range binds on world extent.** `enc` saturates when
  `|isoDepth · effSub · 4| > 65535`, i.e. world iso-depth `> 65535/64 ≈ 1024`
  units at the `effSub = 16` cap (`≈ 16383` at `effSub = 1`). `canvas_stress`'s
  radius-200 GRID orbit reaches `isoDepth ≈ 600 → enc ≈ 38400 < 65535` and fits;
  the #1958 partition clamp saturates anything beyond. **No shipping demo binds
  the ceiling.**

## 2. Two widening paths + tradeoffs

### (a) DEPTH32F attachment — widen the normalize range, float depth

- **Metal: already in production.** Metal's depth attachment is *already*
  `Depth32Float_Stencil8` (it has no 24-bit option). "Current" Metal **is** the
  DEPTH32F path; there is no D24 baseline to compare against on this backend.
- **OpenGL: a one-line, ~free swap.** `GL_DEPTH24_STENCIL8` →
  `GL_DEPTH32F_STENCIL8` is the same 4-byte-depth (+ separate stencil) footprint;
  bandwidth and fill cost are unchanged.
- **Precision is non-uniform — the cost of widening.** Float depth carries
  ~`2^24` exact integer codes across `[0,1]`, **denser near 0, sparser near 1**.
  Raising `kMin/kMax` to use a wider `enc` range shrinks the per-code `[0,1]`
  spacing; the **tie-break low bits (face 0-3, slot) degrade first at the far
  plane** (`gl_FragDepth → 1.0`, ulp `≈ 1.2e-7`). Because `enc` carries `×64` at
  `effSub = 16`, widening to e.g. `±2^23` keeps the low bits exact near the
  centre but loses `+face` ordering for far content. Net: raises the ceiling
  ~64–128× (`~1024 → ~64K–130K` world units at `effSub = 16`) but **stays
  finite**, and **trades GL's uniform precision for float non-uniformity that
  fails far-first**.

### (b) Manual integer R32I composite — true exact 32-bit

- Replace the hardware depth attachment with an `R32I` color target +
  per-fragment `imageAtomicMin` compositing (the per-canvas distance texture
  already works this way), storing raw `enc` as int32. **Retires
  `normalizeDistance` entirely**; exact to `~2^31` uniform codes.
- **Costs:** gives up the hardware depth test / early-Z; adds a per-fragment
  atomic plus a second resolve pass to recover the depth-winner's colour; every
  `*_to_framebuffer` producer must be restructured to write the int. This is a
  substantial pipeline rewrite with real GPU cost — see §3.

## 3. Perf — does it actually cost anything?

Measured (Metal, `IRPerfGrid --mode dense --grid-size 64 --auto-profile 200`):

| Pose | FPS | frame | `trixelToFb` (composite CPU) | `voxelStage1` (atomic write, GPU) |
|---|---|---|---|---|
| Cardinal (yaw 0) | 199.0 | 4.871 ms | **0.044 ms** | 3.807 ms |
| Off-cardinal (yaw 0.6 ≈ 34°) | 88.0 | 11.996 ms | **0.068 ms** | 0.688 ms |

Two readings:

- **The composite / normalize stage is perf-invisible** (`< 0.1 ms` CPU in both
  poses; the GPU side is one full-screen quad). Depth-attachment *precision*
  (D24 vs D32F) therefore has **no measurable perf consequence** — path (a) is
  free on both backends (and Metal already pays it).
- **Atomics are the expensive primitive.** `voxelStage1` — the existing
  `imageAtomicMin` distance-write stage — is the dominant GPU cost (3.8 ms
  cardinal). It is a direct proxy for what path (b)'s manual R32I composite would
  add by multiplying per-fragment atomics across the full-screen composite, on
  top of losing early-Z. Path (b) buys exact 32-bit at a real, measurable cost.

GL: path (a) is a free attachment swap; path (b) carries the same
atomic/early-Z penalty as Metal. A Linux confirmation would only matter *if* a
widening were pursued — it is not (see §5) — so no GL benchmark gates this
finding.

## 4. Does 32-bit let us delete the #1958 partition?

**No.** 32-bit only *raises* the additive-band ceiling; it does not make it
unbounded.

- The #1958 escalation already proved no fixed **additive** band can dominate
  unbounded world placement — the radius-200 orbit exhausted it. 32-bit pushes
  that wall out (path a: `~64–130K` units; path b: `~33M` units at `effSub = 16`)
  but the wall is still there.
- Under **world streaming (#938)** world coordinates are *genuinely* unbounded —
  they grow without limit as the player travels. Any fixed additive band,
  however wide, is eventually exhausted, **reintroducing the "deep world beats
  foreground" bug** the partition was created to kill.
- The partition gives **O(1) dominance-by-membership independent of world
  extent**, is already **byte-identical on the fast path** (a single
  `max`/`clamp`, `f_trixel_to_framebuffer.glsl:100-104`), and costs nothing
  measurable. There is no correctness or perf reason to remove it; 32-bit cannot
  replace what it provides.

## 5. Recommendation

1. **Keep the #1958 disjoint near-plane partition.** It is the only composite
   encoding correct under unbounded streaming. 32-bit raises the additive
   ceiling but never makes it unbounded, so it cannot retire the partition.

2. **Do not widen the depth range / retire `normalizeDistance` now.** No shipping
   demo binds the `±65535` ceiling (`canvas_stress` `enc ≈ 38400` at the
   `effSub-16` cap), both backends already resolve every code exactly, and the
   composite stage is perf-invisible. Widening would add float non-uniformity
   (path a) or a costly atomic rewrite (path b) to buy headroom nothing needs.

3. **Note the backend asymmetry for the record.** GL is D24 (uniform); Metal is
   D32F (non-uniform, hardware-forced). Harmless today — both resolve all 131K
   codes — but it means any *future* range-widening must move GL to
   `GL_DEPTH32F_STENCIL8` **and** re-validate the far-plane face/slot tie-break
   on both backends, trading GL's uniform precision for float non-uniformity. The
   cheap first step, *if a much larger bounded (pre-streaming) world ever forces
   it*, is GL → `GL_DEPTH32F_STENCIL8` + widening `kMin/kMax`, measured on Linux.
   The manual R32I composite (path b) is reserved for a hard exact-unbounded
   requirement that the partition makes unnecessary.

4. **Close #1983 with this finding — no follow-up implementation task.** The
   partition ships unconditionally; no encoding change is recommended.

## References

- Sub-epic #1884; the depth-unification investigation +
  resolution: [`depth-unification-1884-investigation.md`](depth-unification-1884-investigation.md)
  (its "32-bit composite depth (#1983, filed)" stub points here).
- #1958 (B) / PR #1974 — the two-tier disjoint near-plane partition this spike
  evaluated simplifying. Constants + asserts: `ir_render_types.hpp:202-245`.
- World streaming epic #938 — the unbounded-coordinate regime that keeps the
  partition's O(1) correctness winning.
- Encoding: `f_trixel_to_framebuffer.glsl:60-105`; `ir_constants.hpp:47,51`;
  `ir_render_enums.hpp:30`; `opengl_types.hpp:76-77`; `metal_texture.cpp:24-25`.
