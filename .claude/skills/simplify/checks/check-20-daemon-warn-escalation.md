# Check 20 — an added daemon warn must escalate-then-quiet

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds a
log/warn emission (`echo … >&2`, `log "…"`, `… | tee -a "$LOG"`) to a
`scripts/fleet/**` file that contains an unattended repeat loop.

A skip condition in an unattended loop stays true until a human acts, so a
per-pass warn re-emits identically forever and buries the outage it
reports. The rule, counter shape, and alert-file contract:
`scripts/fleet/CLAUDE.md` §"Authoring rules", "An every-tick guard that
warns must escalate-then-quiet".

Three greps over the **changed file**, not the hunk:

1. an unattended loop — `while true` / `while :` with a `sleep`;
2. the added (`+`) emission lines;
3. a consecutive-skip counter or a compliant helper — a
   `${FLEET_STATE_DIR:-…}/.*-skip` counter read/write, `note_gate_block` /
   `clear_gate_block` (`fleet-dispatcher`), `_freshness_warn`
   (`fleet-clone-freshness.sh`), or `escalate_if_hung_lock`
   (`fleet-rebase`).

(1) and (2) with no (3) → flag "escalate-then-quiet, or show the condition
self-clears within the loop". A bare boolean dedup map (`LOGGED["$key"]=1`)
has the quiet half and never the escalate half — a miss, not a pass. The
expected false positive is a transient progress line whose text changes
every pass — surface it for confirmation. Report, don't auto-fix.
