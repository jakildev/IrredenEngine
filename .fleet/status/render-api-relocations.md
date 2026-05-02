# Render API relocations — feature surfaces still on `IRRender::`

Features still on the `IRRender::` namespace that should move to
feature-scoped prefab namespaces, per the rule in
`engine/render/CLAUDE.md` "What belongs in `engine/render/` vs
`engine/prefabs/irreden/render/`".

| Feature | Current (wrong) surface | Tracking task |
|---|---|---|
| Sun lighting | `IRRender::setSunDirection` / `getSunDirection` | T-036 (PR #210) |
| Debug overlay | `IRRender::setDebugOverlay` / `getDebugOverlay` | T-035 (PR #235) |

When a tracking task's PR merges, the queue-manager removes the
corresponding row on its next maintenance pass.
