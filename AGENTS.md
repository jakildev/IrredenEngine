# Irreden Engine — agent entry point

Read [CLAUDE.md](CLAUDE.md) and its linked
[baseline](docs/agents/CLAUDE-BASELINE.md) before working. They remain the
canonical project conventions during the shared-instructions migration.
Before editing a path, read every applicable ancestor `CLAUDE.md` and
the matching path rules under `.claude/rules/`; root startup does not
automatically load descendant instructions. A nested project's own
instructions apply in its scope.

Use [the architecture reference](docs/agents/AGENTS-ARCHITECTURE.md) for
engine structure, [BUILD.md](docs/agents/BUILD.md) for build setup, and
[VALIDATION.md](docs/agents/VALIDATION.md) for the validators that prove a
change.
Fleet sessions also read [FLEET-RUNTIME.md](docs/agents/FLEET-RUNTIME.md)
and [the Codex adapter](docs/agents/CODEX.md). Follow the assigned role
and target; an interactive conversation does not imply autonomous queue pickup.

Shared workflow implementations remain in `docs/agents/skills/` and
`.claude/skills/`. Read the named skill and its referenced procedures when
using it. Claude tool names mean the equivalent available Codex tools;
Claude permission settings are not Codex permissions. Attribute work to
the actual runtime/model, never copy a Claude co-author or generated-by
trailer into Codex-authored work.
