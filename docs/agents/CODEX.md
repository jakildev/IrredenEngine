# Codex fleet adapter

Codex and Claude share project conventions, intent plans, role responsibilities,
claims, acceptance checks, and host-smoke requirements. Runtime-specific
launching, permissions, event parsing, and conversation IDs belong to the
adapter. A model name does not confer a role or permission.

## Instruction and tool compatibility

Read the root `AGENTS.md`, then the assigned role file and its linked protocols.
Role files under `.claude/commands/` are ordinary instructions for Codex, not
slash commands. `Read`, `Grep`, `Glob`, `Write`, and `Bash` refer to equivalent
available tools; use `rg`, explicit command working directories, and file
patches. Bash restrictions motivated solely by Claude's harness belong to
Claude sessions; preserve their underlying rules about quoting, worktree
ownership, and explicit repository selection in both runtimes.

The legacy queue classes `fable`, `opus`, and `sonnet` describe task complexity.
`FLEET_ROLE_MODEL` carries that class independently of the actual model.
Codex design work uses Astra with `xhigh` effort; substantial implementation
and final review use Sol; bounded implementation uses Terra. Luna is suitable
for mechanical tasks when selected explicitly. Keep delegation bounded by
available fleet capacity; do not enable Ultra implicitly inside every worker.

Use the actual model/runtime in authorship and review records. Never imply
that a Claude session implemented or reviewed work performed by Codex.

## Rendering conversations

Before rendering work, read the relevant render module instructions and
the [render-trixel-pipeline](../../.claude/skills/render-trixel-pipeline/SKILL.md),
[render-debug-loop](../../.claude/skills/render-debug-loop/SKILL.md), and
[render-verify](../../.claude/skills/render-verify/SKILL.md) skills. Use
[attach-screenshots](../../.claude/skills/attach-screenshots/SKILL.md) for PR
evidence and [backend-parity](../../.claude/skills/backend-parity/SKILL.md)
with the [cross-host smoke protocol](FLEET-CROSS-HOST-SMOKE.md).

Use Codex's `view_image` for image reads, including every ROI crop.
Inspect actual captured images before describing visual results. Generated
illustrations are not renderer evidence. Record the commit, host/GPU/backend,
scene, camera, viewport, entity/light counts, rotation, shadow settings, and
capture command. Use matching before/after settings; retain full frames and
detail crops when discussing shadow edges or rotation artifacts with the human.

A target above 100 FPS means a frame budget below 10 ms, but a meaningful
criterion also names hardware, resolution, scene population, visible geometry,
lighting/shadow settings, and frame-time statistics. Measure CPU and GPU work
separately with warmup and controlled presentation settings. A screenshot proves
appearance, not throughput; an average FPS number does not prove stable pacing.
Check open issues and PRs before proposing a competing rendering change.

## Planning and review

Use [intent plans](PLANNING-PROTOCOL.md): outcome, locked decisions, constraints,
evidence, runnable acceptance, and explicit escalation conditions. Workers own
implementation choices inside that contract. Both providers may author any
class of work; prefer the other provider for code review. Review the actual
head commit, form an initial assessment independently, then reconcile earlier
findings. Evidence and tests settle disagreements; model agreement is not a
substitute for acceptance checks or cross-host rendering validation.

## Permissions

Sandbox boundaries and command approvals are separate. A trusted repository
does not make protected configuration or Git metadata writable. Unattended
sessions need preconfigured permissions for the assigned worktree, required
Git metadata, fleet state, build/test tools, and GitHub operations. They must
not depend on a human responding to a prompt mid-iteration. A blocked command
must produce a recoverable failure, never a claim-and-retry loop.

Keep authoring and reviewer capabilities distinct, and retain the human merge
boundary. Do not copy Claude's broad interpreter permissions into unsandboxed
Codex rules. Run interpreters and tests inside the workspace sandbox; approve
specific external operations separately. Do not silently switch from a saved
subscription login to API billing.

Official references: [instructions](https://learn.chatgpt.com/docs/agent-configuration/agents-md),
[skills](https://learn.chatgpt.com/docs/build-skills),
[non-interactive execution](https://learn.chatgpt.com/docs/non-interactive-mode),
[permissions](https://learn.chatgpt.com/docs/sandboxing), and
[models](https://learn.chatgpt.com/docs/models).

## Host setup and rollout

Install the fleet scripts with the existing `scripts/fleet/install.sh`.
Authenticate the Codex CLI with `codex login` using ChatGPT, then confirm
`codex login status`. The adapter forces ChatGPT login and removes inherited
API-key overrides. Subscription usage is shared with interactive Codex use;
recorded token counts are telemetry, not a remaining-subscription estimate.

Open and trust the project in Codex once on each host; project-local rules
load only from a trusted configuration layer. From each dedicated worktree, run `fleet-codex --role worker --check`.
This prepares `.codex/rules/fleet.rules` and checks command decisions with
the installed CLI. Repeat with reviewer roles when qualifying a host.
Keep personal rules in separate files: regeneration refuses a manually
modified generated policy. No blanket unsandboxed Python/Bash rule is added.
The launcher writes the role policy before starting the unattended process,
which uses workspace-write, network access, and approval policy `never`.
Unknown or blocked commands return failures rather than waiting for approval.
`--check` verifies command matching only. Run `fleet-codex --role worker --doctor`
to verify actual create/rename/delete access inside the installed CLI's sandbox,
without a model call. It creates missing configured state directories and removes
its temporary probes. Each unattended launch repeats the filesystem probe before
starting a model. A failed probe records the exact paths/errors in
`state/runtime-cooldown/codex.json`, returns exit 2 to preserve an existing resume
sidecar, and pauses Codex dispatch for 15 minutes using the existing provider gate.
Claude remains eligible. Correct the host configuration, run the doctor again,
and let the cooldown expire; no automatic broadening of permissions occurs.
Qualify actual build, GitHub, and screenshot access on each OS before enabling its
pool: filesystem probes do not certify GPU/display access, authentication, or
project-rule trust. See [capability qualification](#capability-qualification).

Workers can edit their assigned checkout, Git metadata, matching downstream
worktree if present, and the necessary fleet state directories. "Git
metadata" is wider than the worktree's own `.git` file: a worktree's history
lives in the main checkout's `.git` (`git rev-parse --git-common-dir`), so the
writable set includes that directory's `hooks/`, `config`, and
`refs/heads/master` too — git genuinely needs most of that reach to operate a
worktree at all. Both git directories are named as explicit roots — the
linked worktree's own gitdir (`.git/worktrees/<pool>`, `git rev-parse
--absolute-git-dir`) and the common dir — because the sandbox carves every
`.git` path out of a writable root as read-only unless a root names that
exact path, and it protects a linked worktree's resolved gitdir separately.
With the common dir alone every git write from the pool (`index.lock`,
`FETCH_HEAD`) fails with "Operation not permitted", so a Codex worker cannot
branch and a Codex reviewer cannot check the PR out. The main checkout is not an additional writable *source*
directory (no reading or editing its working tree), but its `.git` is
reachable through this path. Command rules help prevent common workflow
mistakes, including merge, force-push, and reviewer push; they are not a
security boundary against arbitrary shell programs using the same GitHub
credentials — including a filesystem write straight to
`.git/refs/heads/master` or `.git/hooks/pre-commit` in the main checkout,
which no command rule can see. Keep GitHub branch protection as the merge
boundary, and treat this posture — not a hardened sandbox — as what you are
accepting by enabling a Codex worker on a host.

The matching downstream build directory (`build-game-<worktree>`) and an explicit
`IRREDEN_BUILD_DIR` are writable build outputs too. They follow
`engine/tools/lib/concurrency_helpers.sh`'s build routing; neither requires making
the main source checkout writable. Set build/cache/lock overrides in the host's
launch environment before the session starts; changing a shell variable within
a worker cannot retroactively change its sandbox roots.

## Capability qualification

Claude's `Bash(...)` allowlist and tool hooks are not Codex permissions. Ordinary
Python, Bash, CMake, test runners, and file operations run inside Codex's sandbox.
An absent prefix allow-rule does not itself mean those commands are unavailable.
The generated command policy imports the curated fleet-wrapper entries; it does
not grant unrestricted interpreters. Verify these workflows from an idle,
dedicated checkout on every host and after upgrading Codex:

| Workflow | Qualification | Boundary |
|---|---|---|
| Checkout, branch, index, stash | `--doctor`, then disposable branch/index operations in a qualification checkout | Both resolved Git directories; GitHub branch protection still owns merge authority |
| Claims and shutdown | `--doctor`; inspect a real assigned iteration's claim/release and summary | Configured fleet state roots |
| Build and tests | `fleet-build --target <configured-target>` and the project's test command | Worktree build, matching downstream build, cache and lock paths |
| Screenshots and ROI inspection | The linked rendering skills, `fleet-run` auto-capture, then `view_image` for full frame and crop | Real display/GPU session plus writable capture destination; generated images are not evidence |
| GitHub review and PR publication | Read-only `gh pr view`; confirm a real assigned review or publication succeeds | Login, network and trusted project rules are separate from filesystem access |
| Skills and helpers | Read the named `SKILL.md` and its procedures; use equivalent Codex tools | Claude hooks/slash commands do not automatically execute in Codex |

After a failed checkout, never validate the old checkout and attribute those test
results to the PR. Verify `git rev-parse HEAD` against the PR's `headRefOid` before
running tests. Report a blocked checkout through the assigned completion contract.

### Approval and resume transport

The current unattended transport is `codex exec --json` with approvals `never`.
Its stdout event log is observability, not a bidirectional approval channel. Merely
changing approvals to `on-request` does not connect a worker's request to the human
running the fleet. Use the interactive architect launcher for a human-attended
approval session; permissions granted in one session do not rewrite every pool's
filesystem roots.

A fleet-wide approval inbox needs an app-server client that receives approval
requests, records command/cwd/worktree/target and request IDs durably, delivers the
human decision to that exact pending request, and preserves reservations and
heartbeats while waiting. It must also handle denial, timeout, host restart and
dispatcher capacity without claiming that work completed. That transport is not
implemented here. Preconfigured capabilities and the deterministic preflight are
the supported unattended path. See the official
[sandbox/approval distinction](https://learn.chatgpt.com/docs/sandboxing) and
[app-server protocol](https://learn.chatgpt.com/docs/app-server).

## Enabling mixed-provider dispatch

Set the following in `~/.fleet/fleet-up.conf` after qualifying the host, then
restart the dispatcher:

```bash
FLEET_RUNTIMES="claude,codex"
FLEET_CROSS_PROVIDER_REVIEW=1
FLEET_WORKER_RUNTIME="balanced"
```

Create the four runtime/author labels using `fleet-labels` on each participating
repository before enabling stamping. Include the actual `fleet:author-*` label in
`gh pr create --label ...` so provenance is present when the PR first appears. `fleet:runtime-codex` or
`fleet:runtime-claude` pins an issue/PR. Without a pin, balanced selection hashes
the assignment across that host's available providers. It does not guarantee
equal subscription consumption. All hosts must enable cross-provider review;
a Claude-only host declares `FLEET_RUNTIMES="claude"` and waits when the
required reviewer is Codex. Unstamped PRs wait for explicit provenance; stamp known legacy authors before
enabling the review policy. Do not infer authorship merely from missing labels.
A PR last amended by Codex goes to Claude; one amended by Claude goes to Codex.

The existing claim protocol remains the cross-host authority; provider choice
happens before claiming and launching. Reservations resume the original Codex
target, model, effort, and class. No target-bearing projection means no fresh
live worker. GitHub quota gates both providers; Claude usage gates only Claude.
Codex quota/rate failures start a 15-minute host-local cooldown. Model/auth
failures still use the existing dispatch failure and target circuit breakers.

Current host claim identities use OS keys. Mac, Linux, and Windows retain
their existing shared claim and smoke behavior. Multiple machines with the
same OS need a separate stable machine identity before that topology is safe;
do not repurpose OS smoke labels or a test-only host override as a workaround.
That migration must cover claims, cleanup, heartbeats, and reservations together.

## Interactive Astra architect

Use a dedicated architect worktree at `.claude/worktrees/<architect-name>`,
outside the transient pool. From it:

```bash
fleet-codex --interactive --role opus-architect --model gpt-6-astra --effort xhigh
```

The session starts with the shared architect instructions and rendering-skill
references, then waits for the human to choose the problem. Interactive
approvals remain on-request so a new rendering/capture capability can be
qualified before adding it to unattended permissions. Resume a known session
with `fleet-codex --interactive --resume <session-id>`; the CLI also retains
its normal session history. Architect sessions are not dispatched or claimed
as pool jobs.
