# partner skills — what runs around optimize

Optimize produces perf numbers; the adjacent work belongs to other skills.

| Skill | When | Why |
|---|---|---|
| `render-debug-loop` | before optimize, on render PRs whose visual semantics are unclear | get the picture right, then make it fast |
| `simplify` | after optimize | keeps `IR_PROFILE_*` blocks and perf-rationale comments; catches ECS smells, naming, dead code |
| `render-verify` | after any optimize change to the render pipeline | a frame-time win that breaks a silhouette is not a win |
| `attach-screenshots` | alongside optimize on render PRs | reviewers need the visual diff next to the `compare_perf_runs.py` diff |
| `backend-parity` | after measuring on the authoring backend, when a `*.glsl` / `*.metal` changed | both backends must show the matrix improvement; a one-sided regression is a port bug |
| `polish-checkpoint` | between iteration cycles in a long session | simplify + build without git operations |
| `commit-and-push` | when done | opens the PR; body carries the comparator markdown |
| `start-next-task` | when the session exposed a deeper follow-up | land what you have, stack the deeper fix |

Order: `start-next-task` → optimize (baseline → fix → matrix → compare →
self-improve) → for render PRs `render-verify` + `attach-screenshots` +
`backend-parity` → `commit-and-push` (simplify auto-runs). A step that repeats
across sessions is a skill or a script, not a manual instruction.
