# Scatter-placement integration

The combined renderer at `1aaaba073` includes master `9d9cecc3f` and
the cylinder receiver correction. Its default 21-shot ShapeDebug run
exited CLEAN. Only the two yaw45 references conflicted during the merge.
They are refreshed from this run; no comparison tolerance changed.

The other 19 views have no new drift: 17 are byte-identical to the prior
references, and the two zoom1 views retain the existing 16-pixel/max53
variation documented in the parent evidence.

For each conflicting view, subtracting master pixels from combined pixels
yields exactly the prior cylinder correction, channel by channel:
`combined - master(9d9cecc3f) == previous - master(8a1bc)`. Both have
472 cylinder-delta pixels and zero nonadditive pixels. Thus the new
placement correction and cylinder correction coexist without an
unexplained visual interaction in these views. The three image versions
and machine-readable counts are preserved here.

This is native macOS Debug integration evidence, not Release cost or
Windows qualification.
