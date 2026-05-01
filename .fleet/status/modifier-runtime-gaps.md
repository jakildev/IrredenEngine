# Modifier framework — runtime gaps

Open follow-ups in the modifier framework's runtime.

The current runtime ships all six resolver systems, the manual-call
sweep API, and the pre-destroy hook auto-sweep. One prefab-layer
escape-hatch remains gated on architect review:

- **Stateful lambdas.** `LambdaModifier::fn_` is `std::function<float(float)>`
  — pure base→effective. Patterns that need per-modifier mutable state
  (velocity-drag's hover/blend, glow's hold/fade phases, anything with
  an elapsed-time accumulator) cannot be expressed today. The architect-
  preferred path is the "companion component" shape (entity ID threaded
  into the lambda; state lives in a sibling component on the source).
  Design proposal in `docs/design/modifier_stateful_lambdas.md`; needs
  architect lock-in before implementation.

The stateful-lambda gap is prefab-layer work but has cross-cutting
implications (lambda signature change, new "companion component"
convention) and is gated on architect review.

The pre-destroy hook used for auto-sweep is a generic
`EntityManager::registerPreDestroyHook` mechanism (see
`engine/entity/CLAUDE.md`). Iterating all entities with `C_Modifiers`
on every destruction is O(N) per destroy; for high-churn workloads a
reverse-index (which targets carry modifiers from source X) would
make sweeps O(K) in the per-source modifier count instead. Defer the
index until a profile shows the linear sweep matters.
