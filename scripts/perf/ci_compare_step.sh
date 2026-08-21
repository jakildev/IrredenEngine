#!/usr/bin/env bash
# ci_compare_step.sh — the perf gate's PR-path comparison, extracted from
# .github/workflows/perf-gate.yml so it is testable outside Actions.
#
# Baseline resolution belongs entirely to compare_perf_runs.py's
# resolve_baseline(): this script hands over the baseline ROOT and never
# inspects its layout. A bash-side layout test here would be a second,
# silently-drifting copy of that logic (#2817).
#
# Exit codes mirror check_regression.py:
#     0  no regression (comment posted)
#     1  regression above threshold (comment posted; the workflow's
#        "Fail check on regression" step turns this red)
#   >=2  check_regression.py could not compare at all — stderr is dumped and
#        the exit code propagates so the step goes red. No comment is posted:
#        an infra failure must not masquerade as a perf verdict.
#
# Env:
#   BASELINE_ROOT  baseline root directory (may be empty/absent -> seed-new)
#   HEAD_DIR       head perf run directory (must exist and be non-empty)
#   PR_NUMBER      pull request number to comment on
#   REGRESS_PCT    regression threshold, default 10
#   IMPROVE_PCT    improvement threshold, default 5
#   PERF_TMPDIR    where the comment/stderr artifacts land, default /tmp
#   CHECK_REGRESSION  override the checker invocation (tests shim this)
#   GH_BIN         override the gh binary (tests shim this)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

BASELINE_ROOT="${BASELINE_ROOT:?BASELINE_ROOT is required}"
HEAD_DIR="${HEAD_DIR:?HEAD_DIR is required}"
PR_NUMBER="${PR_NUMBER:?PR_NUMBER is required}"
REGRESS_PCT="${REGRESS_PCT:-10}"
IMPROVE_PCT="${IMPROVE_PCT:-5}"
PERF_TMPDIR="${PERF_TMPDIR:-/tmp}"
CHECK_REGRESSION="${CHECK_REGRESSION:-python3 ${SCRIPT_DIR}/check_regression.py}"
GH_BIN="${GH_BIN:-gh}"

BODY="${PERF_TMPDIR}/perf_comment_body.md"
STDERR="${PERF_TMPDIR}/perf_gate_stderr.txt"
COMMENT="${PERF_TMPDIR}/perf_comment.md"

if [[ ! -d "$HEAD_DIR" ]]; then
  echo "perf-gate: head run directory not found: '${HEAD_DIR}'" >&2
  exit 2
fi

# Echo the head slug on the PR path. The push path has always read it; the PR
# path never did, which is why cross-SKU coverage on the hosted runner pool was
# unmeasurable from a PR run's log (#2817 review finding 1).
HEAD_SLUG=$(python3 -c "
import json, sys
try:
    m = json.load(open(sys.argv[1]))
except Exception:
    m = {}
print((m.get('calibration') or {}).get('host_slug', ''))
" "${HEAD_DIR}/manifest.json")
echo "perf-gate: head host_slug=${HEAD_SLUG:-(none)}"

STATUS=0
# shellcheck disable=SC2086  # CHECK_REGRESSION is an intentionally split command
$CHECK_REGRESSION "$BASELINE_ROOT" "$HEAD_DIR" \
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

"$GH_BIN" pr comment "$PR_NUMBER" --body-file "$COMMENT"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "status=$STATUS" >> "$GITHUB_OUTPUT"
  echo "head_slug=$HEAD_SLUG" >> "$GITHUB_OUTPUT"
fi

exit 0
