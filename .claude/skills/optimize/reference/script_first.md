# script-first for repeatable perf work

Every step the `optimize` skill instructs is either a **script** the agent
invokes verbatim (`scripts/perf/perf_grid_matrix.sh`, `compare_perf_runs.py`,
`perf_summary.py`, `run_math_bench.sh`, `check_regression.py`) or a
**decision** made from the data (which fix to try, whether the delta justifies
the change). Multi-step shell sequences in prose, copy-pasted command
snippets, and "do these N things in order" recipes are bugs — extract them.
Scripts are testable, diffable, and produce the real number instead of a stale
prose estimate.

| Lives in | Contents |
|---|---|
| `scripts/perf/` | matrix run, before/after diff, summary, microbench, regression gate |
| `.claude/skills/optimize/reference/` | bottleneck catalog, big-win lessons, profiling-macro reference, partner skills, this principle |
| `.claude/skills/optimize/SKILL.md` | triage → baseline → scan → profile → fix → measure → report → self-improve |

Two-strike rule: the first time a shell sequence appears in a PR body it is
prose; the second time something similar is needed, extract a script. Not a
script: single-use repro commands tied to one PR (they live in the PR body),
judgement calls, and one-off scratch queries during an investigation.
