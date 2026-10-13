# Finite shadow-index operation counts

Run `python3 scripts/tests/test_render_source_face_index.py`. The harness
executes the production GLSL and Metal indexer bodies with serial host atomics,
counts actual atomic-add/max/exchange calls, and checks the complete buffer against
independently specified tile lists with guard words. No renderer code changes
or GPU readback are required. These are deterministic operation counts, **not
GPU timing, contention measurements, or observations of a live demo**.

## Historical inline-only baseline

The following measurements predate the bounded spill pool. Both backend
adapters produced these counts with 64 inline candidates and no spill nodes;
they are historical operation counts, not the current harness output.

Current limits and failure semantics live in
[finite sun-face index capacity](../design/sun-face-index-capacity.md). The current
harness also checks spill reservations, links, pool exhaustion and stale-frame
clears. Complete queries can retain 256 candidates per tile while the shared
spill pool has capacity.

| Fixture | Allocations attempted | Occupied tiles | Incomplete tiles | Peak tile count | Atomic adds | Atomic maxes |
|---|---:|---:|---:|---:|---:|---:|
| 64 coincident small faces | 64 | 6 | 0 | 64 | 448 | 0 |
| 65 coincident small faces | 65 | 6 | 6 | 65 | 455 | 0 |
| 66 coincident small faces | 66 | 6 | 6 | 66 | 462 | 0 |
| Global record exhaustion sequence | 65,538 | 10 | 10 | 65,535 | 458,750 | 4 |
| 64 faces covering both full maps | 64 | 32,768 | 0 | 64 | 2,097,216 | 0 |
| 65 faces covering both full maps | 65 | 32,768 | 32,768 | 65 | 2,129,985 | 0 |

The global sequence first emits 65,535 faces onto six tiles, fills the last
record on two other tiles, then overflows onto those two and two fresh tiles.
The allocation counter includes failed record reservations; it is not a count
of stored records. Tile-count peaks include candidates beyond list capacity.

## Consequences for optimization

- A single full-map face requires 32,768 tile reservations plus one record
  reservation. [Cooperative box indexing](cooperative-box-face-index.md)
  partitions that tile loop among lanes with one shared record reservation.
  Spilled candidates additionally reserve and link nodes; they are not covered
  by the historical inline-only atomic counts.
- Tile, spill-pool and global record capacity are separate limits. The historical
  65-face full-map failure needs only one spill node per tile under the current
  design and fits the shared pool. Denser full-map overlap can still exhaust
  that pool; increasing record capacity alone cannot help it.
- A saturated tile still performs an atomic add for every additional candidate.
  Avoiding those atomics requires a concurrency-safe saturation protocol and
  measured benefit; preserving bounded writes alone is insufficient proof.
- The current loop covers the projected face's axis-aligned bounding rectangle.
  Conservative face-versus-tile rejection could reduce irrelevant candidates for
  slanted faces, but cannot reduce the full-map fixture. Its benefit must be
  measured separately from scheduling and saturation. Boundary-touching faces
  must never lose a possible receiver hit.

## Validation scope

Both adapters pass the complete-buffer and guard-word oracle, including the
66th candidate, global exhaustion and off-map cases. Existing mutations for
missing cascades, missing overflow marking, tile/record overruns, corrupt
records and off-map allocation still fail. The production runs separately check operation counts per emission against
explicit fixture tile lists. That check is disabled for mutation runs so it
cannot mask a failure of the independent buffer oracle. A future algorithm
that deliberately reduces atomic work must update that cost expectation while
retaining the independent buffer/coverage checks.

Serial execution cannot certify GPU races or performance. The cooperative
adapter separately exercises interleaved ownership; native Metal and OpenGL
behavior, matched captures and the span/count matrix remain distinct checks.
[Current crowded-tile captures](../pr-screenshots/codex/shadow-index-complete-tiles/README.md)
measure spill coverage rather than GPU throughput. No smoothing, bias or
geometry change follows from this operation-count audit.
