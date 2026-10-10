#!/usr/bin/env bash
# ci_compare_step.sh — the perf gate's PR-path comparison, extracted from
# .github/workflows/perf-gate.yml so it is testable outside Actions.
#
# Baseline resolution belongs entirely to compare_perf_runs.py's
# resolve_baseline(): this script hands over the baseline ROOT and never
# inspects its layout. A bash-side layout test here would be a second,
# silently-drifting copy of that logic.
#
# Exit codes mirror check_regression.py:
#     0  no regression (comment posted)
#     1  regression above threshold (comment posted; the workflow's
#        "Fail check on regression" step turns this red)
#   >=2  check_regression.py could not compare at all — stderr is dumped and
#        the exit code propagates so the step goes red. No comment is posted:
#        an infra failure must not masquerade as a perf verdict.
#     3  the comparison finished but the PR comment could not be posted after
#        COMMENT_ATTEMPTS tries (a transient GitHub API error outlasted the
#        retry). stderr names the comment post, not the comparison, as what
#        failed; a single failed post is retried, not fatal.
#
# Env:
#   BASELINE_ROOT  baseline root directory (may be empty/absent -> seed-new)
#   BASELINE_HISTORY  per-capture history root (<slug>/<commit>/); when set,
#                  the checker gates against the class-matched capture
#   HEAD_DIR       head perf run directory (must exist and be non-empty)
#   PR_NUMBER      pull request number to comment on
#   REGRESS_PCT    regression threshold, default 10
#   IMPROVE_PCT    improvement threshold, default 5
#   PERF_TMPDIR    where the comment/stderr artifacts land, default /tmp
#   CHECK_REGRESSION  override the checker invocation (tests shim this)
#   GH_BIN         override the gh binary (tests shim this)
#   COMMENT_ATTEMPTS  total tries for the PR comment post, default 3
#   COMMENT_RETRY_SLEEP  seconds between comment attempts, default 5

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

BASELINE_ROOT="${BASELINE_ROOT:?BASELINE_ROOT is required}"
BASELINE_HISTORY="${BASELINE_HISTORY:-}"
HEAD_DIR="${HEAD_DIR:?HEAD_DIR is required}"
PR_NUMBER="${PR_NUMBER:?PR_NUMBER is required}"
REGRESS_PCT="${REGRESS_PCT:-10}"
IMPROVE_PCT="${IMPROVE_PCT:-5}"
PERF_TMPDIR="${PERF_TMPDIR:-/tmp}"
CHECK_REGRESSION="${CHECK_REGRESSION:-python3 ${SCRIPT_DIR}/check_regression.py}"
GH_BIN="${GH_BIN:-gh}"
COMMENT_ATTEMPTS="${COMMENT_ATTEMPTS:-3}"
COMMENT_RETRY_SLEEP="${COMMENT_RETRY_SLEEP:-5}"

BODY="${PERF_TMPDIR}/perf_comment_body.md"
STDERR="${PERF_TMPDIR}/perf_gate_stderr.txt"
COMMENT="${PERF_TMPDIR}/perf_comment.md"

if [[ ! -d "$HEAD_DIR" ]]; then
  echo "perf-gate: head run directory not found: '${HEAD_DIR}'" >&2
  exit 2
fi

# Echo the head slug on the PR path, read the same way the push path reads it:
# the hosted runner pool spans several CPU SKUs and a run whose SKU has no
# baseline is informational, so the log must say which SKU this run landed on.
HEAD_SLUG=$(python3 -c "
import json, sys
try:
    m = json.load(open(sys.argv[1]))
except Exception:
    m = {}
print((m.get('calibration') or {}).get('host_slug', ''))
" "${HEAD_DIR}/manifest.json")
echo "perf-gate: head host_slug=${HEAD_SLUG:-(none)}"

HISTORY_ARGS=()
if [[ -n "$BASELINE_HISTORY" ]]; then
  HISTORY_ARGS=(--baseline-history "$BASELINE_HISTORY")
fi

STATUS=0
# shellcheck disable=SC2086  # CHECK_REGRESSION is an intentionally split command
$CHECK_REGRESSION "$BASELINE_ROOT" "$HEAD_DIR" ${HISTORY_ARGS[@]+"${HISTORY_ARGS[@]}"} \
  --regress-pct "$REGRESS_PCT" --improve-pct "$IMPROVE_PCT" \
  > "$BODY" 2> "$STDERR" || STATUS=$?

if [[ $STATUS -ge 2 ]]; then
  echo "perf-gate: check_regression.py could not compare (exit ${STATUS}); failing the step." >&2
  cat "$STDERR" >&2
  exit "$STATUS"
fi

# Prepend a header so the comment is self-describing
{
  echo "<!-- perf-gate -->"
  echo "## Perf gate"
  echo ""
  echo "Head host slug: \`${HEAD_SLUG:-(none)}\`"
  echo ""
  cat "$BODY"
  if [[ $STATUS -eq 1 ]]; then
    echo ""
    echo "---"
    echo ":warning: **Regression detected** — at least one cell regressed >${REGRESS_PCT}% on mean frame avg. Fix or justify before merging."
  fi
} > "$COMMENT"

# The verdict is already computed; one transient API error (GraphQL 5xx) must
# not turn the check red with no verdict, so the post is retried a bounded
# number of times.
POSTED=0
for ((attempt = 1; attempt <= COMMENT_ATTEMPTS; attempt++)); do
  if "$GH_BIN" pr comment "$PR_NUMBER" --body-file "$COMMENT"; then
    POSTED=1
    break
  fi
  if ((attempt < COMMENT_ATTEMPTS)); then
    echo "perf-gate: PR comment post failed (attempt ${attempt}/${COMMENT_ATTEMPTS}); retrying in ${COMMENT_RETRY_SLEEP}s." >&2
    sleep "$COMMENT_RETRY_SLEEP"
  fi
done
if [[ $POSTED -eq 0 ]]; then
  echo "perf-gate: the comparison finished (exit ${STATUS}) but the PR comment post failed after ${COMMENT_ATTEMPTS} attempts; failing the step." >&2
  exit 3
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "status=$STATUS" >> "$GITHUB_OUTPUT"
  echo "head_slug=$HEAD_SLUG" >> "$GITHUB_OUTPUT"
fi

exit 0
