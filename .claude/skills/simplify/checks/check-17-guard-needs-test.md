# Check 17 — a new invariant guard with no test proving it fires

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds an `IR_ASSERT` in a non-test file, or deletes a member/flag/special-case with a stated defensive purpose.

`IR_ASSERT` is debug-only, so an untested guard has zero coverage in
*both* configs — it compiles out in release, and nothing proves it fires
in debug (#2425's cadence guard shipped exactly so). Both directions:

- For each `IR_ASSERT(` on an added (`+`) line in a non-test file that
  encodes a **new invariant** (skip restatements of already-tested
  preconditions): look for a test in the diff, or an existing one naming
  the enclosing function, that drives it via `EXPECT_THROW`
  (`test/system/pipeline_groups_test.cpp` is the convention). No hit →
  flag.
- For a diff that **deletes** a member/flag/special-case whose comment or
  name states a defensive purpose: look for a test naming the scenario it
  protected. A redundancy claim is a claim about a scenario, and a
  scenario is precisely what a test encodes — two reviews on #2425
  asserted *opposite* things about the same deleted bit; only a test
  adjudicates (#2438's recurrence).

The new test must be seen to **fail** against the unguarded/pre-fix code —
and when the pre-fix code also errors, it must discriminate on the
diagnostic, not just on "something raised" (#2604). Report, don't
auto-fix — where the test belongs is a judgment call. (#2438)
