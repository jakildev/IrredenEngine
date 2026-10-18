# Finite projected-face envelopes

The producer must index every tile that can contain an accepted receiver, or
mark that tile incomplete. The sun-only `sourceFaceWithinProjectedBounds` gate
limits each projected axis before either divided-coordinate fast acceptance or
the gamma7 closed-edge test. `sourceFaceIndexBounds` encloses every allowed
receiver endpoint. It no longer divides by the projected determinant.

For one axis, treat the stored binary32 values as exact and define:

```
S = |c| + |U| + |V|
L = c + min(0,U) + min(0,V)
H = c + max(0,U) + max(0,V)
```

Every point on the represented parallelogram lies in `[L,H]`, independently
of its orientation or determinant. The receiver evaluates padding
`P = 16*u*computed(S) + 32*eta` and tests the point against computed
`[L-P,H+P]`, with `u=2^-24` and `eta=FLT_MIN`. These constants bound
arithmetic error; they are not pixel dilation or tunable shadow softness.

For the receiver envelope, allow one ULP per basic operation
(`epsilon=2u`) and additive error at most `4*eta` per addition,
including operand/result flushing. Write `gamma(n,e)=n*e/(1-n*e)`;
an omitted second argument means `u`. Two sum operations give
`|computed(S)-S| <= gamma(2,epsilon)*S + 9*eta`.
The padding product and positive final sum each add at most `2*eta`;
the resulting loose bounds are:

```
15*u*S + 26*eta <= P <= 17*u*S + 38*eta
```

Any grouping of the four endpoint terms has three additions and error at
most `gamma(3,epsilon)*(S+P)+13*eta < 7*u*S+14*eta`.
The receiver endpoints therefore retain at least `8*u*S+12*eta` of
outward slack and deviate from the exact endpoints by less than
`24*u*S+52*eta`. A flushed receiver point adds at most one more `eta`.

The producer computes the geometric endpoints and `S` outward, and expands
by outward `32*u*S+64*eta`. This encloses the receiver endpoint and
point-flushing allowances without assuming identical compiler rounding.
Producer basic operations require correctly rounded normal binary32 results;
bit-level outward steps prevent contraction across them.

The normal-or-zero source-input checks, `2^60` magnitude limit, conservative
producer degeneracy guard, safe-cascade gate and outward tile mapping remain
as described below. Invalid producer arithmetic still requests full-cascade
coverage. Poor conditioning alone does not. Nonzero thin faces remain
eligible; there is no new angular cutoff.

The gamma7 predicate still determines the parallelogram footprint inside the
envelope. The AABB is a necessary condition, not a replacement box shadow.
Uncertain hits still clamp inverse coordinates before calculating hit depth.
This preserves represented projected geometry, not arbitrary error accumulated
before the stored world-to-sun projection.

## Numerical domain

The helpers establish a bounded numerical regime. Source corner and
edge magnitudes may not exceed `2^60`; nonzero subnormal or nonfinite source
inputs request full-cascade indexing. Tile division assumes at most four ULPs
of error (`8u` relative for normal results). `sourceFaceCascadeArithmeticSafe` requires
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

## Producer and receiver degeneracy decisions

The producer and receiver compile independently. Their determinant expressions
may contract differently, so comparing only the producer's `abs(determinant)`
with the receiver threshold can discard a face the receiver would accept.
For example, binary32 `U=(8.12339973449707,11.12345027923584)` and `V=2U`
produce zero with separately rounded products, but one FMA contraction gives
approximately `7.1057e-6`. The receiver threshold is `1e-6`.

Let `T=|Ux*Vy|+|Uy*Vx|`. Each determinant evaluation differs from the exact determinant by at most
`gamma(2)*T` plus its underflow allowance. Their mutual difference is therefore
at most `2*gamma(2)*T` plus both absolute allowances, whether either expression
contracts. `sourceFaceDefinitelyDegenerate` encloses the receiver magnitude by
outward evaluation of

```
|producer determinant| + g*T + A
g = ceil_binary32(gamma(16))
A = 32*eta*(1 + ||U||1 + ||V||1)
```

Here `g >= gamma(16) > 2*gamma(2)`, and `A` also covers both determinants'
absolute underflow errors. Only a finite upper bound strictly below `1e-6`
allows the producer to discard the face. Otherwise it proceeds to the envelope
calculation. Poor conditioning alone no longer forces full-cascade indexing.
The receiver determinant threshold remains unchanged.

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

The executable contract should compare the actual index with the finite-envelope
query across adjacent representable tile edges, both windings, translated and
anisotropic cascades, grazing bases, underflow and overflow fallbacks. It should
exercise complete primary and spill lists and incomplete fallback separately.
The diagnostic boundary reproducer establishes one missing membership; it
does not by itself validate this envelope or its native arithmetic assumptions.

The previous inverse-basis containment derivation is retained in the
[producer-only implementation](https://github.com/jakildev/IrredenEngine/blob/5a84e577b62eae376d204970d6944a27a10ae42a/docs/design/sun-face-index-coverage.md).
It demonstrates why narrowing the producer alone would omit accepted receivers.
