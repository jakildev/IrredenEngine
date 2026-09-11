# Check 20 — an added daemon warn must escalate-then-quiet

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds a log/warn emission (`echo … >&2`, `log "…"`,
`… | tee -a "$LOG"`) to a `scripts/fleet/**` file that contains an
unattended repeat loop.

A skip condition in an unattended loop stays true until a human acts, so a
per-pass warn re-emits identically forever — spam that buries the outage it
was meant to report (#2363: one line per minute while every claim on both
repos froze for 30 min). The rule, the counter shape, and the alert-file
contract live in `scripts/fleet/CLAUDE.md` §"Authoring rules", in the "An
every-tick guard that warns must escalate-then-quiet" bullet — the nearest
module `CLAUDE.md`, auto-loaded the moment the script is opened. It was in
front of the author on PR #2772 and again on PR #3098 and the warn shipped
both times, which is why this is a check and not more prose.

Three greps over the **changed file**, not the hunk — the adjacent branches
the diff didn't touch are exactly what a diff-scoped reviewer cannot see:

1. an unattended loop — `while true` / `while :` with a `sleep`;
2. the added (`+`) emission lines;
3. a consecutive-skip counter or a compliant helper — a
   `${FLEET_STATE_DIR:-…}/.*-skip` counter read/write, `note_gate_block` /
   `clear_gate_block` (`fleet-dispatcher`), `_freshness_warn`
   (`fleet-clone-freshness.sh`), or `escalate_if_hung_lock`
   (`fleet-rebase`).

(1) and (2) with no (3) → flag: "escalate-then-quiet, or show the condition
self-clears within the loop". **A bare boolean dedup map is not the
compliant form** — a `LOGGED["$key"]=1` guard has the quiet half and never
the escalate half, and it reads at a glance as though it already handles
this (#3098's `CAP_DEFER_LOGGED`); count it as a miss, not a pass. The
expected false positive is a transient per-pass progress line whose text
changes every pass — surface it for confirmation rather than hard-failing.
Report, don't auto-fix: whether the condition self-clears inside the loop
is the author's call. (#2779)
