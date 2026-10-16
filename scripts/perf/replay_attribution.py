#!/usr/bin/env python3
"""Replay the perf gate's master-movement attribution over its PR comments.

Each class-matched `<!-- perf-gate -->` comment names the merge ref it
measured and the capture it was graded against. For every one, this rebuilds
what that run could see and calls `resolve_attribution` on it:

- the merge ref's first parent, from `gh api repos/<repo>/commits/<head>`,
  fetched by full SHA when it is not local (deleted branches still serve it);
- the `perf-baseline` history, materialized as perf-gate.yml does, truncated
  to the captures whose `perf-baseline` commit is at or before the comment.

A row "fires" when the gate's own `frame_avg_improved` would have labelled
it: some non-load-flagged frame `avg` cell improves raw and calibrated, and
the comment reports no regression. Run it from a checkout with `origin`
fetched; git calls resolve against the working directory.

Usage:
  replay_attribution.py --since 2026-09-29T00:00:00Z --until 2026-10-14T23:59:59Z \\
      [--work DIR] [--stale-master SHA --stale-after TIME]... [--show-pr N]...
"""

import argparse
import collections
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "fleet"))
import compare_perf_runs as cpr  # noqa: E402
import fleet_github  # noqa: E402

MARKER = "<!-- perf-gate -->"
CAPTURE_RE = re.compile(r"baseline capture: `perf-baseline@(\w+)` \([^)]*\); master `(\w+)`")
HEAD_RE = re.compile(r"→ (\S+)-ci")
BASELINE_DIR_RE = re.compile(r"^baseline: `[^`]*/history/([^/`]+)/([0-9a-f]{40})`", re.M)
REF_MS_RE = re.compile(r"ref_ms: head ([\d.]+) vs baseline ([\d.]+)")
# Both row formats: `(+x%)` and the scaled-host `(+x% raw, +y% scaled)`.
FRAME_ROW_RE = re.compile(r"^\| `([^`]+)`( \(load-flagged\))? \| ([\d.]+) → ([\d.]+) "
                          r"\(([-+\d.]+)%(?: raw, [-+\d.]+% scaled)?\)")
REGRESSION_MARK = "Regression detected"


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], capture_output=True)


def fetch_corpus(repo: str, since: str) -> List[Dict]:
    """Every perf-gate comment updated since `since`; the caller filters on
    `created`, because the API's `since` is an updated-at bound."""
    jq = (f'.[] | select(.body | startswith("{MARKER}")) | '
          '{id, url: .issue_url, created: .created_at, body}')
    out = fleet_github.run(
        ["api", "--paginate", f"repos/{repo}/issues/comments?since={since}&per_page=100",
         "--jq", jq], capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.splitlines() if line.strip()]


def parse_comment(comment: Dict) -> Optional[Dict]:
    """The replay row for a class-matched comment; None for any other."""
    body = comment["body"]
    capture = CAPTURE_RE.search(body)
    baseline = BASELINE_DIR_RE.search(body)
    head = HEAD_RE.search(body)
    ref_ms = REF_MS_RE.search(body)
    if not (capture and baseline and head and ref_ms):
        return None
    frames = []
    sections = body.split("## frame timing (ms)")
    if len(sections) > 1:
        for line in sections[1].split("\n## ")[0].splitlines():
            row = FRAME_ROW_RE.match(line)
            if row:
                frames.append((row.group(1), float(row.group(3)), float(row.group(4)),
                               bool(row.group(2))))
    return dict(pr=int(comment["url"].rsplit("/", 1)[1]), created=comment["created"],
                head=head.group(1), slug=baseline.group(1), capture_commit=baseline.group(2),
                capture_master=capture.group(2),
                head_ref_ms=float(ref_ms.group(1)), base_ref_ms=float(ref_ms.group(2)),
                frames=frames, regressed=REGRESSION_MARK in body)


def fires(row: Dict, improve_pct: float) -> bool:
    """The gate's own improvement predicate, on the comment's numbers."""
    ratio = row["base_ref_ms"] / row["head_ref_ms"]
    return not row["regressed"] and any(
        not load_flagged and base > 0.0 and cpr.frame_avg_improved(base, head, ratio, improve_pct)
        for _, base, head, load_flagged in row["frames"])


def materialize_history(baseline_ref: str, history: Path) -> Dict[Tuple[str, str], datetime]:
    """perf-gate.yml's `<slug>/<commit>/` history loop, plus each capture's
    `perf-baseline` commit time."""
    log = git("log", "--format=%H %cI", baseline_ref, "--", "docs/perf/baseline_latest")
    if log.returncode:
        sys.exit(f"replay_attribution: cannot read {baseline_ref}: {log.stderr.decode()}")
    times = {}
    for line in log.stdout.decode().splitlines():
        commit, committed = line.split()
        names = git("diff-tree", "--root", "--no-commit-id", "--name-only", "-r", commit,
                    "--", "docs/perf/baseline_latest").stdout.decode().split()
        for slug in sorted({p.split("/")[3] for p in names if len(p.split("/")) >= 5}):
            src = f"docs/perf/baseline_latest/{slug}"
            if git("cat-file", "-e", f"{commit}:{src}/manifest.json").returncode:
                continue
            times[(slug, commit)] = cpr.parse_utc(committed)
            dest = history / slug / commit
            if dest.is_dir():
                continue
            dest.mkdir(parents=True)
            with tarfile.open(fileobj=io.BytesIO(git("archive", commit, src).stdout)) as tf:
                for member in tf.getmembers():
                    if member.isfile():
                        (dest / os.path.basename(member.name)).write_bytes(
                            tf.extractfile(member).read())
    return times


def visible_view(times: Dict[Tuple[str, str], datetime], created: str,
                 history: Path, views: Path) -> Path:
    """A history root holding only the captures committed at or before `created`."""
    cutoff = cpr.parse_utc(created)
    visible = sorted(key for key, committed in times.items() if committed <= cutoff)
    digest = hashlib.sha1(repr(visible).encode()).hexdigest()[:16]
    root = views / f"{len(visible)}-{digest}"
    if not root.is_dir():
        for slug, commit in visible:
            (root / slug).mkdir(parents=True, exist_ok=True)
            (root / slug / commit).symlink_to(history / slug / commit)
    return root


def resolve_merges(repo: str, heads: List[str], cache_path: Path) -> Dict[str, Optional[List]]:
    """`[sha, [parent, ...]]` per merge head, cached across runs."""
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    for head in heads:
        if head in cache:
            continue
        res = fleet_github.run(["api", f"repos/{repo}/commits/{head}",
                                "--jq", "[.sha, (.parents | map(.sha))]"],
                               capture_output=True, text=True)
        cache[head] = json.loads(res.stdout) if res.returncode == 0 else None
    cache_path.write_text(json.dumps(cache, indent=0, sort_keys=True))
    return cache


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--since", required=True, help="first comment created_at (ISO, Z)")
    parser.add_argument("--until", required=True, help="last comment created_at (ISO, Z)")
    parser.add_argument("--repo", default="jakildev/IrredenEngine")
    parser.add_argument("--work", type=Path, default=Path("save_files/perf/attribution-replay"),
                        help="cache dir for the corpus, merge parents and history")
    parser.add_argument("--corpus", type=Path,
                        help="read the comments from this JSONL instead of the API")
    parser.add_argument("--baseline-ref", default="refs/remotes/origin/perf-baseline")
    parser.add_argument("--trunk", default="refs/remotes/origin/master")
    parser.add_argument("--improve-pct", type=float, default=5.0)
    parser.add_argument("--stale-master", action="append", default=[],
                        help="capture master whose later rows are known stale (repeatable)")
    parser.add_argument("--stale-after",
                        help="rows created after this time on a --stale-master capture are stale")
    parser.add_argument("--show-pr", type=int, action="append", default=[],
                        help="print every row of this PR (repeatable)")
    args = parser.parse_args()
    if bool(args.stale_master) != bool(args.stale_after):
        parser.error("--stale-master and --stale-after go together")

    args.work.mkdir(parents=True, exist_ok=True)
    if args.corpus:
        comments = [json.loads(line) for line in args.corpus.read_text().splitlines() if line]
    else:
        comments = fetch_corpus(args.repo, args.since)
        with open(args.work / "corpus.jsonl", "w") as out:
            out.writelines(json.dumps(c) + "\n" for c in comments)
    since, until = cpr.parse_utc(args.since), cpr.parse_utc(args.until)
    comments = [c for c in comments if since <= cpr.parse_utc(c["created"]) <= until]
    rows = [row for row in map(parse_comment, comments) if row]
    print(f"comments {len(comments)}, class-matched {len(rows)}")

    history = args.work / "history"
    times = materialize_history(args.baseline_ref, history)
    print(f"captures {len(times)} on {len({slug for slug, _ in times})} slugs")

    merges = resolve_merges(args.repo, sorted({r["head"] for r in rows}),
                            args.work / "merges.json")
    parents = sorted({merges[r["head"]][1][0] for r in rows
                      if merges[r["head"]] and len(merges[r["head"]][1]) == 2})
    local = set(cpr._resolve_shas(parents).values())
    missing = [sha for sha in parents if sha not in local]
    for sha in missing:
        git("fetch", "--quiet", "origin", sha)
    print(f"merges unresolved {sum(1 for r in rows if not merges[r['head']])}, "
          f"first parents fetched {len(missing)}")

    stale_masters = {sha[:9] for sha in args.stale_master}
    for row in rows:
        root = visible_view(times, row["created"], history, args.work / "views")
        path = root / row["slug"] / row["capture_commit"]
        if not path.exists():
            row["reason"], row["line"] = "capture not visible", ""
        else:
            manifest = cpr.load_manifest(path)
            capture = cpr.HistoryCapture(path, manifest, cpr.calibration_ref_ms(manifest),
                                         cpr.parse_utc(manifest.get("finished_at")))
            merge = merges[row["head"]]
            base = merge[1][0] if merge and len(merge[1]) == 2 else ""
            attribution = cpr.resolve_attribution(root, capture, base, args.trunk,
                                                  args.improve_pct)
            row["reason"], row["line"] = attribution.reason, attribution.note_line()
        row["fires"] = fires(row, args.improve_pct)
        row["stale"] = bool(stale_masters) and row["capture_master"][:9] in stale_masters \
            and cpr.parse_utc(row["created"]) > cpr.parse_utc(args.stale_after)
    (args.work / "rows.json").write_text(json.dumps(rows, indent=0))

    firing = [r for r in rows if r["fires"]]
    print(f"\nall rows ({len(rows)}): {dict(collections.Counter(r['reason'] for r in rows))}")
    print(f"firing rows ({len(firing)}): "
          f"{dict(collections.Counter(r['reason'] for r in firing))}")
    if stale_masters:
        stale = [r for r in rows if r["stale"]]
        print(f"stale rows {len(stale)}, firing {sum(r['fires'] for r in stale)}, withheld "
              f"{sum(r['reason'] != cpr.ATTRIBUTED for r in stale)}, by reason "
              f"{dict(collections.Counter(r['reason'] for r in stale))}")
    print("\nfiring rows kept:")
    for r in firing:
        if r["reason"] == cpr.ATTRIBUTED:
            print(f"  #{r['pr']} {r['created']}  {r['line']}")
    print("\nfiring rows withheld:")
    for r in sorted(firing, key=lambda r: r["created"]):
        if r["reason"] != cpr.ATTRIBUTED:
            print(f"  #{r['pr']} {r['created']} stale={r['stale']} {r['reason']}")
    for pr in args.show_pr:
        print(f"\n#{pr} rows:")
        for r in rows:
            if r["pr"] == pr:
                print(f"  {r['created']} fires={r['fires']} {r['reason']}: {r['line']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
