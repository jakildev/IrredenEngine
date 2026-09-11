# Check 17 — a new invariant guard with no test proving it fires

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds an
`IR_ASSERT` in a non-test file, or deletes a member/flag/special-case with
a stated defensive purpose.

`IR_ASSERT` is debug-only, so an untested guard has zero coverage in both
configs. Both directions:

- For each `IR_ASSERT(` on a `+` line in a non-test file that encodes a
  **new invariant** (not a restatement of an already-tested
  precondition): look for a test in the diff, or an existing one naming
  the enclosing function, that drives it via `EXPECT_THROW`
  (`test/system/pipeline_groups_test.cpp` is the convention). No hit →
  flag.
- For a diff that **deletes** a member/flag/special-case whose comment or
  name states a defensive purpose: look for a test naming the scenario it
  protected. A redundancy claim is a claim about a scenario, and only a
  test adjudicates it.

The new test must be seen to **fail** against the unguarded/pre-fix code —
and when the pre-fix code also errors, it must discriminate on the
diagnostic, not on "something raised". Report, don't auto-fix — where the
test belongs is a judgment call.
