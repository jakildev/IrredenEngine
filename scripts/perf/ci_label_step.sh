#!/usr/bin/env bash
# Synchronize perf:improved with the verdict owned by the PR's current head.

set -euo pipefail

LABEL_PHASE="${LABEL_PHASE:?LABEL_PHASE is required}"
HEAD_SHA="${HEAD_SHA:?HEAD_SHA is required}"
PR_NUMBER="${PR_NUMBER:?PR_NUMBER is required}"
REPO="${REPO:?REPO is required}"
GATE_STATUS="${GATE_STATUS:-}"
GATE_IMPROVED="${GATE_IMPROVED:-}"
GH_BIN="${GH_BIN:-gh}"
LABEL_ATTEMPTS="${LABEL_ATTEMPTS:-3}"
LABEL_RETRY_SLEEP="${LABEL_RETRY_SLEEP:-5}"

if [[ "$LABEL_PHASE" != "reset" && "$LABEL_PHASE" != "sync" ]]; then
  echo "perf-gate label ${LABEL_PHASE}: expected LABEL_PHASE=reset or sync" >&2
  exit 2
fi

WANT_LABEL=0
if [[ "$LABEL_PHASE" == "sync" && "$GATE_STATUS" == "0" && "$GATE_IMPROVED" == "true" ]]; then
  WANT_LABEL=1
fi

VERIFY_ADD=0
UNDO_ADD=0

read_pr() {
  PR_JSON=$("$GH_BIN" pr view "$PR_NUMBER" --repo "$REPO" --json headRefOid,labels) || return 1
  PR_HEAD=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["headRefOid"])' "$PR_JSON") || return 1
  HAS_LABEL=$(python3 -c 'import json,sys; print("1" if any(x.get("name") == "perf:improved" for x in json.loads(sys.argv[1]).get("labels", [])) else "0")' "$PR_JSON") || return 1
}

remove_label() {
  "$GH_BIN" pr edit "$PR_NUMBER" --repo "$REPO" --remove-label "perf:improved"
}

wait_to_retry() {
  if ((attempt < LABEL_ATTEMPTS)); then
    echo "perf-gate label ${LABEL_PHASE}: state update failed (attempt ${attempt}/${LABEL_ATTEMPTS}); retrying in ${LABEL_RETRY_SLEEP}s." >&2
    sleep "$LABEL_RETRY_SLEEP"
  fi
}

for ((attempt = 1; attempt <= LABEL_ATTEMPTS; attempt++)); do
  if ! read_pr; then
    if ((attempt < LABEL_ATTEMPTS)); then
      echo "perf-gate label ${LABEL_PHASE}: PR read failed (attempt ${attempt}/${LABEL_ATTEMPTS}); retrying in ${LABEL_RETRY_SLEEP}s." >&2
      sleep "$LABEL_RETRY_SLEEP"
    fi
    continue
  fi

  if [[ $UNDO_ADD -eq 1 ]]; then
    if [[ "$HAS_LABEL" == "0" ]]; then
      echo "perf-gate label sync: removed raced improvement label"
      exit 0
    fi
    if remove_label && read_pr && [[ "$HAS_LABEL" == "0" ]]; then
      echo "perf-gate label sync: removed raced improvement label"
      exit 0
    fi
    wait_to_retry
    continue
  fi

  if [[ $VERIFY_ADD -eq 1 ]]; then
    if [[ "$PR_HEAD" != "$HEAD_SHA" ]]; then
      UNDO_ADD=1
      if [[ "$HAS_LABEL" == "0" ]]; then
        echo "perf-gate label sync: raced improvement label already absent"
        exit 0
      fi
      if remove_label && read_pr && [[ "$HAS_LABEL" == "0" ]]; then
        echo "perf-gate label sync: removed raced improvement label"
        exit 0
      fi
      wait_to_retry
      continue
    fi
    if [[ "$HAS_LABEL" == "1" ]]; then
      echo "perf-gate label sync: added perf:improved for current head"
      exit 0
    fi
    VERIFY_ADD=0
  fi

  if [[ "$PR_HEAD" != "$HEAD_SHA" ]]; then
    echo "perf-gate label ${LABEL_PHASE}: superseded head ${HEAD_SHA}; current head ${PR_HEAD} owns the label"
    exit 0
  fi

  if [[ $WANT_LABEL -eq "$HAS_LABEL" ]]; then
    echo "perf-gate label ${LABEL_PHASE}: perf:improved already in wanted state"
    exit 0
  fi

  if [[ $WANT_LABEL -eq 0 ]]; then
    if remove_label; then
      echo "perf-gate label ${LABEL_PHASE}: removed perf:improved"
      exit 0
    fi
    wait_to_retry
    continue
  fi

  if ! "$GH_BIN" pr edit "$PR_NUMBER" --repo "$REPO" --add-label "perf:improved"; then
    wait_to_retry
    continue
  fi
  VERIFY_ADD=1
  if ! read_pr; then
    wait_to_retry
    continue
  fi
  if [[ "$PR_HEAD" == "$HEAD_SHA" && "$HAS_LABEL" == "1" ]]; then
    echo "perf-gate label sync: added perf:improved for current head"
    exit 0
  fi
  if [[ "$PR_HEAD" != "$HEAD_SHA" ]]; then
    UNDO_ADD=1
    if [[ "$HAS_LABEL" == "0" ]]; then
      echo "perf-gate label sync: raced improvement label already absent"
      exit 0
    fi
    if remove_label && read_pr && [[ "$HAS_LABEL" == "0" ]]; then
      echo "perf-gate label sync: removed raced improvement label"
      exit 0
    fi
    wait_to_retry
  fi
done

echo "perf-gate label ${LABEL_PHASE}: failed to reach the wanted label state after ${LABEL_ATTEMPTS} attempts" >&2
exit 1
