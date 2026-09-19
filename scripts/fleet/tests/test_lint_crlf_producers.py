"""Unit, byte-level and whole-tree tests for lint_crlf_producers.py.

The detector flags a `python3 -c` / raw-output `jq` producer (or a same-file
function forwarding one) whose stdout reaches a Bash text consumer — a
`while … read` / `mapfile` / `read`, a string test or `case`, a label
argument, or a `for … in` split — with no CR strip in the producer's own
pipeline. These cases lock that contract, form by form:

  - every consumer form fires; an arithmetic consumer does not
  - a same-file helper forwards its producer to every caller
  - a re-capture (`sed -n 1p`, `${v%%…}`) carries the taint one hop on
  - valid guards clear a site; a guard elsewhere does not
  - comments, quoted fixtures and heredoc bodies are data, not commands
  - LF and CRLF source encodings select identical sites
  - byte oracle: an unguarded consumer keeps the CR on the first of two
    CRLF rows, a guarded one emits exact LF-only rows (no grep, no `$(…)`)
  - the five historical `fleet-claim` work-list producers are named and
    clean; removing any one strip re-flags exactly that site
  - the committed scripts/fleet tree is green with nonzero coverage and a
    frozen-empty exception set

`FLEET_CRLF_LINTER=<abs path>` selects the detector under test (a historical
control stages an old tree that never contained one); the scanned root is
always this file's own scripts/fleet, so the control reads the staged tree.
stdlib-only; every fixture lives under a TemporaryDirectory.
"""
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

_FLEET_DIR = Path(__file__).resolve().parent.parent
_LINTER = Path(os.environ.get("FLEET_CRLF_LINTER") or _FLEET_DIR / "lint_crlf_producers.py")


def _load_linter():
    # A missing detector is a setup failure, never a skip: the historical
    # control must fail loudly if the override points nowhere.
    if not _LINTER.is_file():
        raise RuntimeError(f"detector not found: {_LINTER} (set FLEET_CRLF_LINTER)")
    spec = importlib.util.spec_from_file_location("lint_crlf_producers", _LINTER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lint = _load_linter()


def _findings(text, name="fixture.sh"):
    return lint.analyze_text(name, textwrap.dedent(text))


def _lines(text):
    return [f.line for f in _findings(text)]


def _run_main(*roots):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = lint.main(["lint_crlf_producers.py", *map(str, roots)])
    return rc, out.getvalue(), err.getvalue()


class TmpTree(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body, mode=None):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(textwrap.dedent(body).encode("utf-8"))
        if mode is not None:
            path.chmod(mode)
        return path


# --- Consumer forms -----------------------------------------------------------

class DirectConsumers(unittest.TestCase):
    def test_pipe_into_while_read_fires(self):
        found = _findings("""
            python3 -c 'print("a")' | while IFS= read -r l; do echo "$l"; done
        """)
        self.assertEqual([(f.line, f.consumer) for f in found], [(2, "`read` via pipe")])

    def test_process_substitution_into_done_fires(self):
        found = _findings("""
            while IFS=$'\\t' read -r n label; do
                echo "$n"
            done < <(python3 -c 'print("1\\tx")')
        """)
        self.assertEqual([(f.line, f.consumer) for f in found],
                         [(4, "`done` process substitution")])

    def test_process_substitution_into_mapfile_fires(self):
        found = _findings("""
            mapfile -t rows < <(jq -r '.[]' "$f")
        """)
        self.assertEqual([(f.line, f.producer, f.consumer) for f in found],
                         [(2, "jq -r", "`mapfile` process substitution")])

    def test_here_string_capture_into_read_fires(self):
        found = _findings("""
            counts=$(python3 -c 'print(0, 0, 0)')
            read -r a b c <<< "$counts"
        """)
        self.assertEqual([(f.line, f.variable, f.consumer) for f in found],
                         [(2, "counts", "`read` here-string")])

    def test_here_string_capture_into_mapfile_fires(self):
        found = _findings("""
            all=$(python3 -c 'print("x")' 2>/dev/null || echo "")
            local -a arr
            mapfile -t arr <<<"$all"
        """)
        self.assertEqual([(f.line, f.consumer) for f in found], [(2, "`mapfile` here-string")])

    def test_here_string_capture_into_done_fires(self):
        found = _findings("""
            plan=$(gh issue list | python3 -c 'print("1\\tfleet:claim-x")' 2>/dev/null || echo "")
            while IFS=$'\\t' read -r n label; do
                gh issue edit "$n" --remove-label "$label"
            done <<< "$plan"
        """)
        self.assertEqual([(f.line, f.consumer) for f in found], [(2, "`done` here-string")])

    def test_for_in_word_split_fires(self):
        found = _findings("""
            for l in $(python3 -c 'print("a b")'); do echo "$l"; done
        """)
        self.assertEqual([(f.line, f.consumer) for f in found], [(2, "`for … in` word split")])

    def test_for_over_array_capture_fires(self):
        found = _findings("""
            arr=($(jq -r '.[]' "$f"))
            for x in "${arr[@]}"; do echo "$x"; done
        """)
        self.assertEqual([(f.line, f.variable) for f in found], [(2, "arr")])


class CaptureConsumers(unittest.TestCase):
    def test_string_equality_fires(self):
        found = _findings("""
            state=$(printf '%s' "$json" | python3 -c 'import json; print(json.loads(input())["s"])')
            if [[ "$state" == "MERGED" ]]; then echo merged; fi
        """)
        self.assertEqual([(f.line, f.consumer) for f in found], [(2, "string test `==`")])

    def test_empty_test_fires(self):
        # `print("")` is `\\r\\n` on Windows and `$(…)` trims only the `\\n`, so
        # `-z` on the capture reads a non-empty string.
        self.assertEqual(_lines("""
            holders=$(python3 -c 'print("")' 2>/dev/null || echo "")
            [[ -z "$holders" ]] && return 1
        """), [2])

    def test_inequality_inside_conjunction_fires(self):
        self.assertEqual(_lines("""
            v=$(jq -r '.x' "$f")
            if [[ -n "$other" && "$v" != "none" ]]; then :; fi
        """), [2])

    def test_single_bracket_and_test_builtin_fire(self):
        self.assertEqual(_lines("""
            a=$(jq -r '.a' "$f")
            b=$(jq -r '.b' "$f")
            [ "$a" = "x" ] && test "$b" != "y"
        """), [2, 3])

    def test_case_match_fires(self):
        found = _findings("""
            role=$(python3 -c 'print("worker")' "$sc" 2>/dev/null || true)
            case "$role" in
                worker|merger) touch "$dir/$role" ;;
            esac
        """)
        self.assertEqual([(f.line, f.consumer) for f in found], [(2, "`case` match")])

    def test_label_argument_fires_in_both_flag_forms(self):
        found = _findings("""
            l=$(jq -r '.label' "$f")
            m=$(jq -r '.label' "$f")
            gh issue edit 1 --remove-label "$l"
            gh issue edit 1 --add-label="$m"
        """)
        self.assertEqual([(f.line, f.consumer) for f in found],
                         [(2, "label argument `--remove-label`"),
                          (3, "label argument `--add-label`")])

    def test_inline_substitution_in_test_fires(self):
        self.assertEqual(_lines("""
            if [[ $(jq -r --arg l "$l" '[.labels[].name] | index($l)' "$f") == "null" ]]; then :; fi
        """), [2])

    def test_two_hop_recapture_through_sed_fires(self):
        found = _findings("""
            fields=$(python3 -c "print('r'); print('i'); print('l')" "$s" 2>/dev/null || echo "")
            repo=$(echo "$fields"  | sed -n '1p')
            label=$(echo "$fields" | sed -n '3p')
            gh issue edit 1 --repo "$repo" --remove-label "$label"
        """)
        self.assertEqual([(f.line, f.variable, f.consumer) for f in found],
                         [(2, "label", "label argument `--remove-label`")])

    def test_parameter_expansion_hop_fires(self):
        self.assertEqual(_lines("""
            both=$(python3 -c 'print("a","b",sep="\\t")' "$f" 2>/dev/null || echo $'\\t')
            wt="${both%%$'\\t'*}"
            task="${both##*$'\\t'}"
            [[ -z "$wt" || -z "$task" ]] && return 0
        """), [2])

    def test_arithmetic_consumers_are_out_of_contract(self):
        # A numeric capture read only by `(( ))` / `-eq` / `-gt` is a different
        # failure (bash arithmetic rejects a stray CR loudly).
        self.assertEqual(_lines("""
            n=$(python3 -c 'print(3)')
            m=$(python3 -c 'print(0)')
            (( n > 0 )) && echo many
            if (( $n > 0 )); then echo many; fi
            while (( m < $n )); do m=$(( m + 1 )); done
            [[ "$m" -eq 0 ]] && echo none
            [[ "$n" -gt 2 ]] && echo more
        """), [])

    def test_display_only_capture_is_clean(self):
        self.assertEqual(_lines("""
            sid=$(python3 -c 'import uuid; print(uuid.uuid4())')
            echo "session $sid"
            log "starting $sid"
        """), [])

    def test_reassignment_clears_taint(self):
        self.assertEqual(_lines("""
            v=$(jq -r '.x' "$f")
            v="literal"
            [[ "$v" == "literal" ]] && echo yes
        """), [])

    def test_one_finding_per_producer(self):
        # Three consumers of one capture report the producer once.
        found = _findings("""
            v=$(jq -r '.x' "$f")
            [[ -z "$v" ]] && exit 1
            case "$v" in a) ;; esac
            gh pr edit 1 --add-label "$v"
        """)
        self.assertEqual(len(found), 1)


# --- Same-file forwarding ---------------------------------------------------

class Forwarding(unittest.TestCase):
    FETCH = """
        fetch_issue_info() {
            local json
            json=$(gh issue view "$1" --json state,labels) || return 0
            printf '%s' "$json" | python3 -c '
        import json, sys
        d = json.load(sys.stdin)
        print("state\\t" + d["state"])
        print("labels\\t" + " ".join(l["name"] for l in d["labels"]))
        '
        }
    """

    def test_helper_forwards_producer_to_its_callers(self):
        found = _findings(self.FETCH + """
            check_blockers() {
                local info
                info=$(fetch_issue_info "$1") || true
                [[ -z "$info" ]] && return 0
                while IFS=$'\\t' read -r key value; do :; done <<< "$info"
            }
        """)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].function, "check_blockers")
        self.assertEqual(found[0].variable, "info")
        self.assertIn("fetch_issue_info() (forwards python3 -c from line", found[0].producer)

    def test_helper_guarded_inside_clears_every_caller(self):
        guarded = self.FETCH.replace("'\n        }", "' | tr -d '\\r'\n        }")
        self.assertIn("| tr -d '\\r'", guarded)
        self.assertEqual(_lines(guarded + """
            check_blockers() {
                local info
                info=$(fetch_issue_info "$1") || true
                [[ -z "$info" ]] && return 0
            }
        """), [])

    def test_guard_at_the_caller_clears_that_call(self):
        self.assertEqual(_lines(self.FETCH + """
            a=$(fetch_issue_info 1 | tr -d '\\r')
            [[ -z "$a" ]] && exit 1
        """), [])

    def test_echo_of_a_tainted_local_forwards(self):
        found = _findings("""
            field() {
                local out
                out=$(printf '%s' "$1" | python3 -c 'import json; print(json.loads(input())["k"])')
                echo "$out"
            }
            atype=$(field "$line")
            case "$atype" in x) ;; esac
        """)
        self.assertEqual([(f.line, f.variable) for f in found], [(7, "atype")])

    def test_helper_that_consumes_internally_is_not_a_producer(self):
        self.assertEqual(_lines("""
            count_rows() {
                python3 -c 'print("a")' | while IFS= read -r l; do :; done
            }
            n=$(count_rows)
            [[ "$n" == "" ]] && echo none
        """), [3])   # the internal pipe is the only finding, at its own line

    def test_inline_call_in_a_test_fires(self):
        self.assertEqual(_lines("""
            fp() { python3 -c 'print("abc")' "$1" 2>/dev/null || echo absent; }
            [[ "$(fp "$snap")" != "$expected" ]] && echo changed
        """), [3])


# --- Guards -----------------------------------------------------------------

class Guards(unittest.TestCase):
    def _consumer(self, producer_line):
        return f"""
            v=$({producer_line})
            [[ "$v" == "x" ]] && echo x
        """

    def test_tr_spellings_are_guards(self):
        for guard in ("tr -d '\\r'", 'tr -d "\\r"', "tr -d $'\\r'", "tr -d '\\r\\n'",
                      "tr -d '\\015'", "tr --delete '\\r'", "dos2unix"):
            with self.subTest(guard=guard):
                self.assertEqual(_lines(self._consumer(f"jq -r '.x' \"$f\" | {guard}")), [])

    def test_guard_before_the_fallback_arm_counts(self):
        self.assertEqual(_lines(self._consumer(
            "python3 -c 'print(1)' 2>/dev/null | tr -d '\\r' || echo \"\"")), [])

    def test_guard_only_on_the_fallback_arm_does_not_count(self):
        # `a || b | tr` binds as `a || (b | tr)`: the producer is unguarded.
        self.assertEqual(_lines(self._consumer(
            "python3 -c 'print(1)' 2>/dev/null || echo \"\" | tr -d '\\r'")), [2])

    def test_guard_after_a_filter_still_counts(self):
        self.assertEqual(_lines(self._consumer(
            "jq -r '.[]' \"$f\" | head -1 | tr -d '\\r'")), [])

    def test_tr_without_cr_is_not_a_guard(self):
        self.assertEqual(_lines(self._consumer("jq -r '.x' \"$f\" | tr -d ' '")), [2])

    def test_guard_on_an_unrelated_pipeline_does_not_count(self):
        self.assertEqual(_lines("""
            other=$(jq -r '.y' "$f" | tr -d '\\r')
            v=$(jq -r '.x' "$f")
            [[ "$v" == "x" ]] && echo x
        """), [3])

    def test_upstream_strip_does_not_count(self):
        self.assertEqual(_lines("""
            v=$(cat "$f" | tr -d '\\r' | jq -r '.x')
            [[ "$v" == "x" ]] && echo x
        """), [2])

    def test_post_capture_cleanup_does_not_count(self):
        self.assertEqual(_lines("""
            v=$(jq -r '.x' "$f")
            v="${v//$'\\r'/}"
            [[ "$v" == "x" ]] && echo x
        """), [2])

    def test_comment_naming_the_guard_does_not_count(self):
        self.assertEqual(_lines("""
            # tr -d '\\r' would go here
            v=$(jq -r '.x' "$f")
            [[ "$v" == "x" ]] && echo x
        """), [3])


# --- Producer shapes --------------------------------------------------------

class ProducerShapes(unittest.TestCase):
    def test_multiline_program_with_env_prefix_and_continuation(self):
        found = _findings("""
            plan=$(gh issue list --repo "$repo" --state open \\
                --json number,labels --limit 200 2>/dev/null \\
                | HOST_PREFIX="$prefix" python3 -c '
        import json, os, sys
        prefix = os.environ.get("HOST_PREFIX", "")
        for issue in json.load(sys.stdin):
            for l in issue.get("labels") or []:
                if l["name"].startswith(prefix):
                    print("%s\\t%s" % (issue["number"], l["name"]))
        ' 2>/dev/null || echo "")
            [[ -z "$plan" ]] && continue
        """)
        # The producer's own line, not the assignment's.
        self.assertEqual([(f.line, f.producer, f.variable) for f in found],
                         [(4, "python3 -c", "plan")])

    def test_jq_flag_spellings(self):
        for flags, fires in (("-r", True), ("--raw-output", True), ("-rc", True),
                             ("-c", False), ("", False)):
            with self.subTest(flags=flags):
                self.assertEqual(_lines(f"""
                    v=$(jq {flags} '.x' "$f")
                    [[ "$v" == "x" ]] && echo x
                """), [2] if fires else [])

    def test_python_spellings(self):
        for cmd, fires in (("python3 -c", True), ("python -c", True),
                           ("/usr/bin/python3 -c", True), ("python3 script.py", False)):
            with self.subTest(cmd=cmd):
                self.assertEqual(_lines(f"""
                    v=$({cmd} 'print(1)')
                    [[ "$v" == "1" ]] && echo one
                """), [2] if fires else [])

    def test_unsafe_producer_beside_a_safe_one_still_fires(self):
        self.assertEqual(_lines("""
            mapfile -t want_remove < <(jq -r '.remove[]?' <<<"$edge" | tr -d '\\r')
            scope=$(jq -r '.scope' <<<"$edge")
            [[ "$scope" == "issue" ]] && echo issue
        """), [3])

    def test_scoped_locals_do_not_leak_across_functions(self):
        # `x` is tainted only inside `a`; `b` has its own clean `x`.
        self.assertEqual(_lines("""
            a() {
                local x
                x=$(jq -r '.x' "$f")
                echo "$x"
            }
            b() {
                local x="literal"
                [[ "$x" == "literal" ]] && echo yes
            }
        """), [])


# --- Data is not code -------------------------------------------------------

class DataIsNotCode(unittest.TestCase):
    OFFENDER = """v=$(jq -r '.x' "$f"); [[ "$v" == x ]] && echo x"""

    def test_comment_does_not_fire(self):
        self.assertEqual(_lines(f"""
            # {self.OFFENDER}
            echo ok
        """), [])

    def test_quoted_fixture_does_not_fire(self):
        self.assertEqual(_lines(f"""
            printf '%s\\n' '{self.OFFENDER}' > "$STUB"
            printf '%s\\n' "{self.OFFENDER}" > "$STUB"
        """), [])

    def test_heredoc_body_does_not_fire(self):
        self.assertEqual(_lines(f"""
            cat > "$STUB" <<'EOF'
            {self.OFFENDER}
            EOF
            cat > "$STUB" <<EOF
            {self.OFFENDER}
            EOF
            cat > "$STUB" <<-EOF
            \t{self.OFFENDER}
            \tEOF
            echo done
        """), [])

    def test_statement_after_a_heredoc_is_still_code(self):
        self.assertEqual(_lines(f"""
            cat > "$STUB" <<'EOF'
            harmless
            EOF
            {self.OFFENDER}
        """), [5])

    def test_python_heredoc_producer_is_out_of_contract(self):
        self.assertEqual(_lines("""
            v=$(python3 - <<'PY'
            print("x")
            PY
            )
            [[ "$v" == x ]] && echo x
        """), [])


class Encodings(unittest.TestCase):
    def test_lf_and_crlf_sources_select_identical_sites(self):
        src = textwrap.dedent("""
            v=$(jq -r '.x' "$f")
            [[ "$v" == "x" ]] && echo x
            w=$(python3 -c 'print(1)' | tr -d '\\r')
            [[ "$w" == "1" ]] && echo one
        """)
        lf = lint.analyze_text("lf.sh", src)
        crlf = lint.analyze_text("crlf.sh", src.replace("\n", "\r\n"))
        self.assertEqual([(f.line, f.consumer) for f in lf], [(2, "string test `==`")])
        self.assertEqual([(f.line, f.consumer) for f in crlf],
                         [(f.line, f.consumer) for f in lf])


# --- File selection and exit codes ------------------------------------------

class FilesAndExit(TmpTree):
    OFFENDER = """
        v=$(jq -r '.x' "$f")
        [[ "$v" == "x" ]] && echo x
    """

    def test_selection_by_extension_and_shebang(self):
        self.write("a.sh", self.OFFENDER)
        self.write("b.bash", self.OFFENDER)
        self.write("fleet-tool", "#!/usr/bin/env bash\n" + self.OFFENDER)
        self.write("posix-tool", "#!/bin/sh\n" + self.OFFENDER)
        self.write("py-tool", "#!/usr/bin/env python3\n" + self.OFFENDER)
        self.write("notes.md", self.OFFENDER)
        self.write("mod.py", self.OFFENDER)
        self.write("__pycache__/x.sh", self.OFFENDER)
        self.write("no-shebang", self.OFFENDER)
        self.assertEqual(sorted(p.name for p in lint.iter_files([self.root])),
                         ["a.sh", "b.bash", "fleet-tool", "posix-tool"])

    def test_findings_exit_one_with_posix_refs(self):
        self.write("a.sh", self.OFFENDER)
        rc, out, err = _run_main(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("a.sh:2: unguarded jq -r stdout captured into $v", out)
        self.assertIn("swept 1 Bash file(s)", err)

    def test_clean_tree_exits_zero_with_coverage(self):
        self.write("a.sh", "v=$(jq -r '.x' \"$f\" | tr -d '\\r')\n[[ \"$v\" == x ]]\n")
        rc, out, err = _run_main(self.root)
        self.assertEqual((rc, out), (0, ""))
        self.assertIn("swept 1 Bash file(s)", err)

    def test_zero_file_scope_exits_two(self):
        self.write("notes.md", "prose only\n")
        rc, _, err = _run_main(self.root)
        self.assertEqual(rc, 2)
        self.assertIn("zero Bash files", err)
        self.assertEqual(_run_main(self.root / "missing")[0], 2)

    def test_option_like_argument_is_a_usage_error(self):
        self.assertEqual(_run_main("--bogus")[0], 2)

    def test_file_root_is_scanned_as_given(self):
        path = self.write("a.sh", self.OFFENDER)
        self.assertEqual(_run_main(path)[0], 1)


# --- Byte oracle ------------------------------------------------------------

class BytePositiveControl(TmpTree):
    """Prove the failure the detector guards against, in bytes.

    A `python3` shadow on PATH CRLF-terminates the real interpreter's stdout
    and tees the bytes it injected. The unguarded consumer keeps the CR on
    the first of two rows; the guarded one emits exact LF-only rows. Asserted
    on binary reads — grep and `$(…)` both drop the CR on the host that has
    the bug, so neither is an oracle."""

    ROWS = b"fleet:label-a\r\nfleet:label-b\r\n"

    def _run(self, guard):
        # POSIX spellings throughout: the stub and the script are Bash, and an
        # MSYS2 bash on a hand-run Windows host reads `C:/…`, not `C:\…`.
        real = Path(sys.executable).as_posix()
        stub_dir = self.root / "bin"
        injected = (self.root / "injected.bin").as_posix()
        self.write("bin/python3", f"""
            #!/usr/bin/env bash
            "{real}" "$@" | "{real}" -c 'import sys
            d = sys.stdin.buffer.read().replace(b"\\n", b"\\r\\n")
            sys.stdout.buffer.write(d)' | tee -a "{injected}"
        """, mode=0o755)
        out = self.root / f"rows-{'guarded' if guard else 'unguarded'}.bin"
        out_posix = out.as_posix()
        script = self.write("consume.sh", f"""
            set -euo pipefail
            labels=$(python3 -c 'print("fleet:label-a"); print("fleet:label-b")'{guard})
            while IFS= read -r l; do
                printf '%s\\n' "$l" >> "{out_posix}"
            done <<< "$labels"
        """)
        env = dict(os.environ, PATH=f"{stub_dir}{os.pathsep}{os.environ.get('PATH', '')}")
        subprocess.run(["bash", str(script)], env=env, check=True)
        return Path(injected).read_bytes(), out.read_bytes()

    def test_unguarded_consumer_keeps_the_cr_on_the_first_row(self):
        injected, rows = self._run("")
        self.assertEqual(injected, self.ROWS)
        self.assertEqual(rows.split(b"\n")[0], b"fleet:label-a\r")

    def test_guarded_consumer_emits_exact_lf_rows(self):
        injected, rows = self._run(" | tr -d '\\r'")
        self.assertEqual(injected, self.ROWS)
        self.assertEqual(rows, b"fleet:label-a\nfleet:label-b\n")

    def test_detector_agrees_with_the_bytes(self):
        unguarded = "labels=$(python3 -c 'print(1)')\nwhile read -r l; do :; done <<< \"$labels\"\n"
        self.assertEqual([f.line for f in lint.analyze_text("c.sh", unguarded)], [1])
        self.assertEqual(lint.analyze_text("c.sh", unguarded.replace("')", "' | tr -d '\\r')")), [])


# --- Historical sites and the committed tree --------------------------------

def _fleet_claim_findings(text):
    return lint.analyze_text("fleet-claim", text)


class HistoricalSites(unittest.TestCase):
    """The five work-list producers the prose rule missed, named one per test.

    A site is (enclosing function, a phrase from its Python program): the
    program text is what stays stable across the tree revisions a control
    stages, where capture-variable names do not.
    Each test asserts the site still exists (an absent site would pass
    vacuously) and that the detector reports it clean. Against the pre-fix
    tree every one of these fails — that is the positive control."""

    SITES = (
        ("cmd_cleanup_gh",
         'name.startswith("fleet:reviewing-") or name.startswith("fleet:resolving-")'),
        ("cmd_cleanup_gh", 'name.startswith("fleet:claim-")'),
        ("cmd_cleanup_gh", 'name.startswith("fleet:stewarding-")'),
        ("cmd_cleanup_gh", 'name.startswith("fleet:planning-")'),
        ("cmd_reset_sweep_host_claims", "name.startswith(prefix)"),
    )
    STRIP = "| tr -d '\\r'"

    @classmethod
    def setUpClass(cls):
        cls.path = _FLEET_DIR / "fleet-claim"
        if not cls.path.is_file():
            raise RuntimeError(f"fleet-claim not found beside this suite: {cls.path}")
        cls.text = cls.path.read_text(encoding="utf-8")
        cls.findings = _fleet_claim_findings(cls.text)

    @classmethod
    def _split(cls, text, function):
        head, rest = text.split(f"\n{function}() {{", 1)
        body, tail = rest.split("\n}\n", 1)
        return head + f"\n{function}() {{", body, "\n}\n" + tail

    def _assert_clean(self, function, anchor):
        self.assertIn(f"\n{function}() {{", self.text, f"{function}() must exist")
        self.assertIn(anchor, self._split(self.text, function)[1],
                      f"{function} must still carry the producer {anchor!r}")
        hits = [f for f in self.findings if f.function == function and anchor in f.command]
        self.assertEqual(hits, [], "\n".join(f.render() for f in hits))

    def test_cleanup_gh_pr_label_sweep_is_stripped(self):
        self._assert_clean(*self.SITES[0])

    def test_cleanup_gh_open_claim_sweep_is_stripped(self):
        self._assert_clean(*self.SITES[1])

    def test_cleanup_gh_steward_sweep_is_stripped(self):
        self._assert_clean(*self.SITES[2])

    def test_cleanup_gh_planning_sweep_is_stripped(self):
        self._assert_clean(*self.SITES[3])

    def test_reset_sweep_host_claims_open_claim_sweep_is_stripped(self):
        self._assert_clean(*self.SITES[4])

    def test_each_strip_is_load_bearing(self):
        # Removing one site's strip re-flags exactly that site: the detector
        # reaches every historical producer on the current tree.
        for function, anchor in self.SITES:
            with self.subTest(site=f"{function}::{anchor}"):
                head, body, tail = self._split(self.text, function)
                pre, post = body.split(anchor, 1)
                self.assertIn(self.STRIP, post)
                post = post.replace(self.STRIP, "", 1)
                hits = [(f.function, anchor in f.command)
                        for f in _fleet_claim_findings(head + pre + anchor + post + tail)]
                self.assertEqual(hits, [(function, True)])


class CommittedTree(unittest.TestCase):
    def test_committed_fleet_tree_is_green_with_coverage(self):
        rc, out, err = _run_main(_FLEET_DIR)
        self.assertEqual(rc, 0, out)
        self.assertRegex(err, r"swept [1-9]\d* Bash file\(s\)")

    def test_a_new_unsafe_file_turns_the_tree_red(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "new-tool.sh"
            bad.write_text("v=$(jq -r '.x' \"$f\")\n[[ \"$v\" == x ]] && echo x\n")
            rc, out, _ = _run_main(_FLEET_DIR, bad)
            self.assertEqual(rc, 1)
            self.assertIn("new-tool.sh:1:", out)

    def test_exception_set_is_frozen_empty(self):
        # Shrink-only from zero: any entry here is a baseline that grew.
        self.assertEqual(lint.EXEMPT_FILES, frozenset())
        self.assertIsInstance(lint.EXEMPT_FILES, frozenset)


if __name__ == "__main__":
    unittest.main()
