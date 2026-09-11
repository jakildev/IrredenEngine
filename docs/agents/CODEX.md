# Codex fleet adapter

Codex and Claude share project conventions, intent plans, role
responsibilities, claims, acceptance checks, and host-smoke requirements;
launching, permissions, event parsing, and conversation IDs belong to the
adapter. A model name confers no role or permission. This file owns only
the Codex deltas.

## Instruction and tool compatibility

Read the root `AGENTS.md`, then the assigned role file and its linked
protocols. Role files under `.claude/commands/` are ordinary instructions,
not slash commands. `Read`, `Grep`, `Glob`, `Write`, and `Bash` mean the
equivalent Codex tools (`rg`, explicit working directories, file patches).
Bash restrictions motivated only by Claude's harness do not apply; their
underlying rules about quoting, worktree ownership, and explicit
repository selection do.

The queue classes `fable` / `opus` / `sonnet` describe task complexity;
`FLEET_ROLE_MODEL` carries the class independently of the model. Design
work uses Astra at `xhigh`; substantial implementation and final review
Sol; bounded implementation Terra; Luna only when selected explicitly for
mechanical tasks; never Ultra implicitly inside a worker. Record the
actual model and runtime in authorship and review; never imply a Claude
session did work Codex did.

## Rendering conversations

Before render work read the render module instructions and the
[render-trixel-pipeline](../../.claude/skills/render-trixel-pipeline/SKILL.md),
[render-debug-loop](../../.claude/skills/render-debug-loop/SKILL.md), and
[render-verify](../../.claude/skills/render-verify/SKILL.md) skills; use
[attach-screenshots](../../.claude/skills/attach-screenshots/SKILL.md) for
PR evidence and
[backend-parity](../../.claude/skills/backend-parity/SKILL.md) with
[cross-host smoke](FLEET-CROSS-HOST-SMOKE.md). Image reads, including
every ROI crop, go through `view_image`; inspect captured images before
describing them, and a generated illustration is never renderer evidence.
Record commit, host/GPU/backend, scene, camera, viewport, entity/light
counts, rotation, shadow settings, and capture command; match
before/after settings; keep full frames and detail crops for shadow-edge
or rotation discussions. A frame-rate criterion names hardware,
resolution, scene population, visible geometry, lighting/shadow settings,
and frame-time statistics, measured CPU and GPU separately with warmup; a
screenshot proves appearance, not throughput. Check open issues and PRs
before proposing a competing render change.

## Planning and review

Use [intent plans](PLANNING-PROTOCOL.md); workers own implementation
choices inside that contract. Either provider may author any class of
work; prefer the other provider for review. Review the actual head commit,
form an independent assessment, then reconcile earlier findings. Evidence
and tests settle disagreements; model agreement is not an acceptance
check.

## Permissions

Sandbox boundaries and command approvals are separate; a trusted
repository does not make protected configuration or git metadata
writable. Unattended sessions need preconfigured permissions for the
assigned worktree, required git metadata, fleet state, build/test tools,
and GitHub operations; a blocked command produces a recoverable failure,
never a claim-and-retry loop or a prompt a human must answer
mid-iteration. Keep authoring and reviewer capabilities distinct and the
human merge boundary intact; run interpreters and tests inside the
workspace sandbox and approve specific external operations separately;
never copy Claude's broad interpreter permissions into unsandboxed rules;
never silently switch from a saved subscription login to API billing.
References:
[instructions](https://learn.chatgpt.com/docs/agent-configuration/agents-md),
[skills](https://learn.chatgpt.com/docs/build-skills),
[non-interactive execution](https://learn.chatgpt.com/docs/non-interactive-mode),
[permissions](https://learn.chatgpt.com/docs/sandboxing),
[models](https://learn.chatgpt.com/docs/models).

## Host setup and rollout

1. `scripts/fleet/install.sh`; `codex login` with ChatGPT; `codex login
   status`. The adapter forces ChatGPT login and drops inherited API-key
   overrides; token counts are telemetry, not a remaining-quota estimate.
2. Open and trust the project in Codex once per host (project rules load
   only from a trusted layer).
3. From each dedicated worktree, `fleet-codex --role worker --check`
   writes `.codex/rules/fleet.rules` and checks command decisions against
   the installed CLI; repeat for reviewer roles. Keep personal rules in
   separate files — regeneration refuses a manually modified generated
   policy. No blanket unsandboxed Python/Bash rule is added.
4. `fleet-codex --role worker --doctor` probes real create/rename/delete
   access inside the sandbox without a model call. Every unattended launch
   repeats it; a failure records paths and errors in
   `state/runtime-cooldown/codex.json`, exits 2 (preserving a resume
   sidecar), and pauses Codex dispatch 15 minutes while Claude stays
   eligible. Fix the host, re-run the doctor, let the cooldown expire.
5. Qualify build, GitHub, and screenshot access per OS before enabling its
   pool (below); filesystem probes certify none of GPU/display,
   authentication, or project-rule trust.

The launcher writes the role policy, then runs with workspace-write,
network access, and approval policy `never`; unknown or blocked commands
fail rather than wait. Writable roots: the assigned checkout; both
resolved git directories — the linked worktree's gitdir (`git rev-parse
--absolute-git-dir`) and the common dir (`git rev-parse --git-common-dir`,
including `hooks/`, `config`, `refs/heads/master`), named explicitly
because the sandbox carves every `.git` path out of a writable root unless
a root names it; the downstream game worktree and `build-game-<worktree>`;
an explicit `IRREDEN_BUILD_DIR`; the configured fleet state directories.
The main checkout's working tree is not writable, and build/cache/lock
overrides are set in the launch environment (a worker cannot change its
own roots). Command rules prevent common workflow mistakes (merge,
force-push, reviewer push) but are not a security boundary against
arbitrary programs holding the same GitHub credentials; GitHub branch
protection stays the merge boundary, and enabling a Codex worker accepts
that posture.

## Capability qualification

Claude's `Bash(...)` allowlist and hooks are not Codex permissions;
ordinary Python, Bash, CMake, tests, and file operations run inside the
sandbox regardless of prefix rules, and the generated policy grants no
unrestricted interpreter. Verify from an idle dedicated checkout on every
host and after upgrading Codex:

| Workflow | Qualification | Boundary |
|---|---|---|
| Checkout, branch, index, stash | `--doctor`, then disposable branch/index operations | both resolved git directories; branch protection owns merge |
| Claims and shutdown | `--doctor`; inspect a real assigned iteration's claim/release and summary | configured fleet state roots |
| Build and tests | `fleet-build --target <configured-target>` and the project's test command | worktree build, downstream build, cache and lock paths |
| Screenshots and ROI inspection | the render skills, `fleet-run` auto-capture, `view_image` on frame and crop | real display/GPU session and a writable capture destination |
| GitHub review and PR publication | read-only `gh pr view`; a real assigned review or publication | login, network, trusted rules are separate from filesystem access |
| Skills and helpers | read the named `SKILL.md` and procedures; use equivalent tools | Claude hooks / slash commands do not run in Codex |

After a failed checkout, verify `git rev-parse HEAD` against the PR's
`headRefOid` before testing — never attribute the old checkout's results
to the PR — and report a blocked checkout through the completion contract.

### Approval and resume transport

Unattended transport is `codex exec --json` with approvals `never`; its
event log is observability, not an approval channel, and `on-request`
alone connects nothing to the human. Human-attended approval is the
interactive architect launcher, and a permission granted there does not
rewrite any pool's roots. A fleet-wide approval inbox is not implemented;
preconfigured capabilities plus the deterministic preflight are the
supported path
([sandbox/approval distinction](https://learn.chatgpt.com/docs/sandboxing),
[app-server protocol](https://learn.chatgpt.com/docs/app-server)).

## Enabling mixed-provider dispatch

After qualifying the host, in `~/.fleet/fleet-up.conf`, then restart the
dispatcher:

```bash
FLEET_RUNTIMES="claude,codex"
FLEET_CROSS_PROVIDER_REVIEW=1
FLEET_WORKER_RUNTIME="balanced"
```

Create the `fleet:author-*` / `fleet:runtime-*` labels with `fleet-labels`
on each repo first
([`fleet-labels-reference.md § Provider routing`](fleet-labels-reference.md));
pass the `fleet:author-*` label in `gh pr create --label` so provenance
exists from the first tick; stamp known legacy authors before enabling
the review policy; never infer authorship from a missing label.
`balanced` hashes assignments across the host's available providers.
Every host must enable cross-provider review; a Claude-only host declares
`FLEET_RUNTIMES="claude"` and waits when the required reviewer is Codex. A
PR last amended by Codex goes to Claude and vice versa.

Claims stay the cross-host authority; provider choice precedes claim and
launch. Reservations resume the original Codex target, model, effort, and
class; no target-bearing projection means no fresh live worker. GitHub
quota gates both providers, Claude usage gates only Claude, Codex
quota/rate failures start a 15-minute host-local cooldown, and model/auth
failures use the existing dispatch-failure and target circuit breakers.
Host identities are OS keys; two machines with the same OS need a
separate stable machine identity (claims, cleanup, heartbeats,
reservations together) before that topology is safe — never repurpose
smoke labels or a test-only host override.

## Interactive Astra architect

From a dedicated architect worktree at `.claude/worktrees/<architect-name>`,
outside the transient pool:

```bash
fleet-codex --interactive --role opus-architect --model gpt-6-astra --effort xhigh
```

It starts with the shared architect instructions and render-skill
references, then waits for the human; approvals stay on-request so a new
capability can be qualified before it enters unattended permissions.
Resume with `fleet-codex --interactive --resume <session-id>`. Architect
sessions are never dispatched or claimed as pool jobs.
