# Conservative finite-face indexing bounds

The producer must index every tile that can contain an accepted receiver, or
mark that tile incomplete. The finite receiver's rounded closed predicate is
wider than the exact projected parallelogram. Its index cannot use an
unexpanded geometric AABB. The
[executable boundary witness](../perf/sun-face-query-cost-followup/README.md#diagnosed-indexquery-boundary-mismatch--historical-reproduction)
demonstrates the mismatch independently of rendered images.

`ir_sun_face_bounds` computes a sun-space envelope on the producer. It leaves
the receiver predicate, divided coordinates and clamped hit depth unchanged.
`sourceFaceIndexBounds` returns false when its finite envelope cannot be
established; the caller must then index the full cascade, retaining the normal
capacity and incomplete-list rules.

## Arithmetic contract

The derivation uses binary32, unit roundoff `u = 2^-24`, and
`gamma(n) = n*u/(1-n*u)`. Normal addition, subtraction and multiplication have
relative error at most `u`; contraction into an FMA is allowed. Division has
relative error at most `8u` for normal results, sufficient for an error of four
ULPs. This is an explicit required backend capability, not a claim that an
arbitrary fast-math implementation satisfies it. A host test compiled with
contraction disabled does not certify GPU division accuracy or reassociation.

Subnormal source corner/edge/determinant values request full-cascade indexing.
Exact positive and negative zero are allowed. Receiver values or intermediate
results that flush to zero contribute an absolute error bounded using
`eta = FLT_MIN`. Nonfinite input and envelope results are detected from their
binary32 exponent bits, avoiding a floating `isfinite` check that fast-math
could remove.

**Receiver edge and tolerance arithmetic must not overflow.** A finite face
alone does not prove this: with `c=(0,0)`, `U=(2,0)`, `V=(0,2)` and sufficiently
large receiver coordinates, numerator and tolerance can become infinite. The
existing predicate can then accept its `inf >= -inf` comparisons.

The helpers establish a bounded numerical regime instead. Source corner and
edge magnitudes may not exceed `2^60`. `sourceFaceCascadeArithmeticSafe` requires
normal-or-zero origin coordinates of magnitude at most `2^58`, and positive
normal tile cell sizes at most `2^50`. Outside this regime the producer indexes
the full cascade; it does not change visibility or discard the face.

A receiver whose normalized tile coordinates are in `[0,128)` then has
`|p-origin| <= ((128+eta)*cellSize/(1-8u)+3*eta)/(1-u)`, hence `|p| < 2^60`.
Together with the source bounds this puts each numerator scale below `2^122`
and the determinant magnitude sum below `2^121`, leaving ample headroom for
their rounded sums and differences. A divided face coordinate may overflow
when the determinant is small; this cannot pass the `[0,1]` fast test, and
the undivided tolerant predicate remains finite.

The receiver's float-to-integer tile conversion must have defined semantics.
An out-of-range or nonfinite conversion cannot be used to infer an in-map
receiver domain; the numerical argument does not assign meaning to undefined
integer conversions. This is a precondition of the existing sampler as well.

## From query arithmetic to exact coordinates

Treat stored input floats as exact real values. Let

```
D = Ux*Vy - Uy*Vx
T = |Ux*Vy| + |Uy*Vx|
Nu = (px-cx)*Vy - (py-cy)*Vx
Nv = Ux*(py-cy) - Uy*(px-cx)
Su = (|px|+|cx|)*|Vy| + (|py|+|cy|)*|Vx|
Sv = (|px|+|cx|)*|Uy| + (|py|+|cy|)*|Ux|
```

For finite normal operations, the rounded numerator differs from `Nu` by at
most `gamma(3)*Su`; determinant error is at most `gamma(2)*T`. The computed
gamma7 tolerance is at most `gamma(7)*(1+8u)*(1+u)^5*(Su+T)`: the `8u` factor
covers its constant division and five stages cover the positive expression.
An accepted upper-edge subtraction can round toward zero, so its tolerance
must additionally be divided by `1-u` before constraining the unrounded
numerator. Combining these terms gives a coefficient below `gamma(11)`.

The divided-coordinate fast path is included separately. An accepted normal
quotient has an exact ratio at most `1/(1-8u)`. Applying numerator and
determinant error bounds again gives a coefficient below `gamma(11)`.
A negative quotient rounded or flushed to negative zero cannot be excluded
by a strict nonnegative-numerator test; its absolute contribution belongs in
the underflow allowance.

The implementation uses `g = ceil_binary32(gamma(16))`, represented by bits
`0x35800009`. This exceeds both normal-path bounds. Its absolute allowance is

```
H = 32 * eta * (1 + ||U||1 + ||V||1)
```

The absolute budget covers receiver-input flushing, coordinate-subtraction
underflow before multiplication by an edge, product and final-subtraction
underflow, determinant underflow error, and evaluation of the tolerance.
For one numerator, the first group is bounded by
`eta*(3 + 2*||otherEdge||1)`; determinant evaluation adds at most `3*eta`.
The positive tolerance expression and accepted upper-edge subtraction fit
inside the remaining budget, including their factors below two. The factor
32 bounds arithmetic error measured in floating units; it is not a pixel or
tile dilation. Division flushing contributes `eta*|rounded D|`, absorbed by
the relative margin since `eta` is much smaller than `u`.

After orienting by the determinant's sign, every accepted exact numerator is
therefore in

```
[-g*(Su+T)-H, |D|+g*(Su+T)+H]
```

and analogously for `Nv`. A positive denominator in the construction below
also establishes that the rounded and exact determinants have the same sign.

## Producer and receiver degeneracy decisions

The producer and receiver compile independently. Their determinant expressions
may contract differently, so comparing only the producer's `abs(determinant)`
with the receiver threshold can discard a face the receiver would accept.
For example, binary32 `U=(8.12339973449707,11.12345027923584)` and `V=2U`
produce zero with separately rounded products, but one FMA contraction gives
approximately `7.1057e-6`. The receiver threshold is `1e-6`.

Each determinant evaluation differs from the exact determinant by at most
`gamma(2)*T` plus its underflow allowance. Their mutual difference is therefore
at most `2*gamma(2)*T` plus both absolute allowances, whether either expression
contracts. `sourceFaceDefinitelyDegenerate` encloses the receiver magnitude by
outward evaluation of

```
|producer determinant| + g*T + H
```

Here `g >= gamma(16) > 2*gamma(2)`, and `H` also covers both determinants'
absolute underflow errors. Only a finite upper bound strictly below `1e-6`
allows the producer to discard the face. Otherwise it proceeds to the envelope
calculation, whose uncertain or nonpositive denominator requests full-cascade
indexing. The receiver predicate and its threshold remain unchanged.

This can make exactly parallel but poorly conditioned projected bases expensive:
their separate floating evaluations need not agree on degeneracy. Do not
restore the producer-only threshold to reduce that cost. Measure the fallback
frequency and index work alongside correctness controls with independently
contracted determinant expressions.

## Solve the point-dependent error bound

The query tolerance depends on `|p|`, so evaluating it only at the original
face corners is insufficient. A scalar fixed-point bound avoids searching for
the enlarged corners. Define exact nonnegative quantities

```
C = ||c||inf                 A = ||U||inf
B = ||V||inf                R = C+A+B
K = A*||V||1 + B*||U||1     J = (A+B)*T
r = ||p||inf
```

The exact inverse face coordinates are `Nu/D` and `Nv/D`. Clamping those
coordinates to `[0,1]` gives a point on the geometric face. Their excess implies
an infinity-norm displacement at most

```
[g*((r+C)*K+J) + (A+B)*H] / |D|
```

Consequently `r <= R + displacement`. Solving this inequality yields geometric
AABB padding

```
delta = [g*((R+C)*K+J) + (A+B)*H] / (|D|-g*K)
```

The computable denominator uses the lower bound
`|rounded D| - g*(T+K) - H`. Nonpositive denominators, nonfinite intermediate
results, and nonfinite final bounds request full-cascade indexing. The fallback
is essential: a nearly singular projected basis can have a large accepted
region even though the receiver's determinant threshold passes.

All nonnegative numerator terms are rounded outward. The denominator is rounded
down. `sourceFaceBoundUp` and `sourceFaceBoundDown` step the binary32 encoding
outward; values at or below the minimum normal magnitude use a normal enclosure
that also survives flushing. The final positive division is inflated by a
binary32 factor exceeding `1/(1-8u)` and rounded outward. Geometric AABB sums
and their final padding subtraction/addition are also rounded outward.

## Tile mapping and integration obligations

`sourceFaceTileInterval(low, high, origin, cellSize)` returns an enclosing
normalized interval **before floor**. Its caller first verifies the cascade
arithmetic regime. The mapper does not assume separately compiled native fast
divisions are identical or monotone.

Outward subtraction bounds enclose the exact endpoint differences. With `M`
their maximum absolute magnitude, expanding by `u*M+3*eta` includes receiver
subtraction rounding, receiver-input flushing, and flushing of a subnormal
difference supplied to division. Divide the resulting endpoints by the positive
cell size to get computed quotients `qL` and `qH`. Write

```
e = 8u
Q = max(|qL|, |qH|)
E = (e*Q+eta)/(1-e)
```

Each exact quotient differs from its producer result by at most `E`; every
exact receiver quotient is therefore between `qL-E` and `qH+E`. The receiver's
own division can add error at most `e*(Q+E)+eta = E`. Thus the interval
`[qL-2E, qH+2E]` covers the receiver result, even when producer and receiver
division errors have opposite signs. The helper computes this expansion
outward, using a binary32 factor exceeding `1/(1-8u)`. Nonfinite mapping
intermediates return `[-FLT_MAX, FLT_MAX]`, requesting the entire tile range.

Apply floor and clamp these floating interval endpoints before integer
conversion, using first-tile range `[0,128]` and last-tile range `[-1,127]`.
Out-of-map and full-domain values must not overflow integer casts. The safe
cascade gate, spatial envelope, outward tile interval, and bounded conversion
are all necessary parts of the producer's inclusion contract.

The executable contract should compare the actual index with the unchanged
query across adjacent representable tile edges, both windings, translated and
anisotropic cascades, grazing bases, underflow and overflow fallbacks. It should
exercise complete primary and spill lists and incomplete fallback separately.
The diagnostic boundary reproducer establishes one missing membership; it
does not by itself validate this envelope or its native arithmetic assumptions.
