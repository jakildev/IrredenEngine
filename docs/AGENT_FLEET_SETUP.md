# Parallel-agent workflow: external setup checklist

This is the list of things **you** (the user) need to do once, outside
the repo, to run the parallel-agent workflow described in the top-level
`CLAUDE.md`. Nothing in the repo can set these up for you — they're
environment, auth, and process.

Work through the list top-to-bottom. Each step is independent unless
noted.

## Fleet environment: WSL2 + tmux on your Windows PC

The parallel-agent fleet runs inside **WSL2** (Ubuntu 24.04) on the
Windows host, managed with **tmux**. Reasons:

- One shell environment (bash, apt, systemd optional) that every agent
  shares — no more `cmd.exe /c "set PATH=...`" wrappers around every
  build command.
- tmux gives a stable, nameable session you can attach/detach from
  multiple terminals and that survives you closing Windows Terminal.
- The WSL filesystem (`ext4` on `\\wsl$`) is dramatically faster for git
  and CMake than `/mnt/c/...`, which matters when five worktrees are
  each running builds.
- Linux is the environment we're maturing the engine *toward* anyway
  (see `CLAUDE.md` — most of the engine was originally Windows, and
  making all three platforms — Windows, macOS, Linux — work is a
  first-class goal).

The existing Windows-native clone at
`C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine` stays — you'll
keep using it for VSCode-native work, validating Windows builds, and
anything that needs the MSYS2 mingw64 toolchain directly. The fleet
clone inside WSL is **separate** and independent; they are two
checkouts with two build trees.

---

## 1. WSL2 + Ubuntu + base tooling

If WSL2 isn't already installed, from an elevated PowerShell on the
Windows host:

```powershell
wsl --install -d Ubuntu-24.04
```

This installs WSL2 (if needed) and Ubuntu 24.04 LTS. Reboot when asked.
On first launch, Ubuntu asks for a username + password — use the same
handle you use for git (this becomes the Linux account the fleet runs
under).

After the install, confirm it's WSL2 and not WSL1:

```powershell
wsl -l -v
```

`VERSION` should read `2`. If it says `1`, convert:

```powershell
wsl --set-version Ubuntu-24.04 2
```

### Inside WSL, install the base toolchain

Open an Ubuntu shell (Start → Ubuntu, or `wsl -d Ubuntu-24.04` from
PowerShell) and install everything the fleet needs in one shot:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y \
    build-essential gcc-13 g++-13 cmake ninja-build pkg-config git \
    curl wget unzip \
    tmux \
    libgl1-mesa-dev libglu1-mesa-dev xorg-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols \
    libasound2-dev libpulse-dev libjack-jackd2-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libavdevice-dev libavfilter-dev \
    clang-format clang-tidy \
    python3 python3-venv
```

Notes on what these give you:

- `build-essential gcc-13 g++-13` — the engine is C++23, so you need
  gcc ≥ 13. Ubuntu 24.04 ships gcc 13 as `g++-13`; confirm with
  `g++-13 --version`. If you want it to be the default `g++`, run
  `sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100`.
- `cmake ninja-build pkg-config` — build system and config tools.
  Ubuntu 24.04 ships CMake ≥ 3.28, which is what the preset needs.
- Xorg / Wayland dev headers — GLFW needs these at build time even
  though most rendering dependencies are fetched via `FetchContent`.
- `libasound2-dev libpulse-dev libjack-jackd2-dev` — RtAudio on Linux
  uses ALSA / Pulse / JACK; the audio CMakeLists checks for these.
- `libav*-dev` — FFmpeg development headers for the video subsystem
  (see `engine/video/CLAUDE.md`). Found via `pkg-config`.
- `clang-format clang-tidy` — match the Windows toolchain so the
  `format` / `format-check` / `lint` targets produce identical output
  across platforms.

### Configure git inside WSL

```bash
git config --global user.name  "Evin Killian"
git config --global user.email "<same email you use for GitHub>"
git config --global init.defaultBranch master
git config --global pull.rebase false
```

### Clone the engine into the WSL filesystem (not /mnt/c!)

```bash
mkdir -p ~/src && cd ~/src
git clone git@github.com:jakildev/IrredenEngine.git
cd IrredenEngine
```

`~/src/IrredenEngine` is the canonical fleet clone path used in the
rest of this document. If you pick a different path, substitute it
everywhere below.

**Do not** clone into `/mnt/c/Users/evinj/...`. The `/mnt/c` reflector
is slow enough for CMake configure + git status that the fleet will be
bottlenecked on filesystem I/O. Keep fleet state on the WSL-native
ext4 volume.

If you haven't set up an SSH key for GitHub in WSL yet, do that now:

```bash
ssh-keygen -t ed25519 -C "<email>"
cat ~/.ssh/id_ed25519.pub   # paste into github.com/settings/keys
ssh -T git@github.com       # should greet you
```

---

## 2. GitHub CLI install + auth (inside WSL)

Required for the `commit-and-push` and `review-pr` skills. Install via
the official GitHub apt repo so you get a current version:

```bash
(type -p wget >/dev/null || sudo apt install wget -y) && \
sudo mkdir -p -m 755 /etc/apt/keyrings && \
wget -qO- https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    | sudo tee /etc/apt/keyrings/githubcli-archive-keyring.gpg > /dev/null && \
sudo chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg && \
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
    | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null && \
sudo apt update && \
sudo apt install gh -y
```

Verify and auth:

```bash
gh --version
gh auth login
```

Pick **GitHub.com → SSH → Upload existing key (or use the one you just
made) → Login with a web browser**. The browser opens on the Windows
host via WSLg — this works transparently on Windows 11 and on Windows 10
with recent updates. If the browser doesn't open, pick the
"paste a one-time code" flow instead and open the URL manually in your
Windows browser.

Scopes you need: `repo`, `read:org`, `workflow`. Verify with:

```bash
gh auth status
```

The output should list those scopes under "Token scopes".

---

## 3. Install Claude Code inside WSL

Claude Code lives inside the WSL distro, not on the Windows side. Two
common install paths:

**Via npm** (simplest if you already have Node):

```bash
# Install Node.js 20 LTS via nvm if you don't have it
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
# (close and reopen your shell so nvm is on PATH)
nvm install 20 && nvm use 20

# Then Claude Code
npm install -g @anthropic-ai/claude-code
claude --version
```

**Via the install script** (if you prefer not to manage Node): follow
whatever install path is current on the Claude Code docs for Linux.

From inside any worktree, you start a session with plain `claude`. The
Bash tool inside that session is already Linux — no `cmd.exe` wrapping,
no PATH workarounds.

---

## 4. Permanent worktrees for the agent fleet

The existing `.claude/worktrees/` on the Windows clone are ephemeral,
created ad-hoc by the old workflow. For the fleet, create named
worktrees **inside the WSL clone** and reuse them across sessions.

A reasonable starting setup with the model split in mind:

| Worktree              | Model  | Role                                |
|-----------------------|--------|-------------------------------------|
| `opus-architect`      | Opus   | Core engine work, ECS/render/audio  |
| `sonnet-fleet-1`      | Sonnet | TASKS.md items, tests, docs         |
| `sonnet-fleet-2`      | Sonnet | TASKS.md items, tests, docs         |
| `sonnet-reviewer`     | Sonnet | First-pass PR review                |
| `opus-reviewer`       | Opus   | Second-pass review + merge sign-off |

Create them once:

```bash
cd ~/src/IrredenEngine

git worktree add .claude/worktrees/opus-architect master
git worktree add .claude/worktrees/sonnet-fleet-1 master
git worktree add .claude/worktrees/sonnet-fleet-2 master
git worktree add .claude/worktrees/sonnet-reviewer master
git worktree add .claude/worktrees/opus-reviewer master
```

These live forever. Each agent session opens the worktree it wants and
the `start-next-task` skill resets its branch back to `origin/master`
after each PR, so the worktree itself is reused but the branch name
changes each task.

You can delete the old ephemeral worktrees (`adoring-gates`,
`goofy-jones`, `vectorized-sauteeing-yeti`) from the Windows-native
clone once this PR merges — they were from the bootstrap phase and have
no persistent role in the fleet.

---

## 5. tmux session layout

tmux is how you keep all five fleet sessions visible and
attachable/detachable from one terminal. A simple starting layout is
one tmux session named `fleet` with one window per worktree role.

First-time setup: drop a minimal `~/.tmux.conf` that makes prefix `C-a`
(easier to reach than default `C-b`) and gives you mouse scroll:

```bash
cat > ~/.tmux.conf <<'EOF'
set -g prefix C-a
unbind C-b
bind C-a send-prefix
set -g mouse on
set -g history-limit 20000
set -g base-index 1
setw -g pane-base-index 1
set -g status-right "#(whoami)@#H | %Y-%m-%d %H:%M"
EOF
```

Create a helper script that spins up the whole fleet layout in one
command. Save as `~/bin/fleet-up`:

```bash
mkdir -p ~/bin
cat > ~/bin/fleet-up <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

REPO="$HOME/src/IrredenEngine"
SESSION="fleet"

if tmux has-session -t "$SESSION" 2>/dev/null; then
    echo "fleet session already exists — attach with: tmux attach -t $SESSION"
    exit 0
fi

# opus-architect: first window
tmux new-session -d -s "$SESSION" -n opus-arch \
    -c "$REPO/.claude/worktrees/opus-architect"

# sonnet fleet and reviewers in subsequent windows
tmux new-window -t "$SESSION":2 -n sonnet-1 \
    -c "$REPO/.claude/worktrees/sonnet-fleet-1"
tmux new-window -t "$SESSION":3 -n sonnet-2 \
    -c "$REPO/.claude/worktrees/sonnet-fleet-2"
tmux new-window -t "$SESSION":4 -n sonnet-rev \
    -c "$REPO/.claude/worktrees/sonnet-reviewer"
tmux new-window -t "$SESSION":5 -n opus-rev \
    -c "$REPO/.claude/worktrees/opus-reviewer"

tmux select-window -t "$SESSION":1
echo "fleet session created. attach with: tmux attach -t $SESSION"
EOF
chmod +x ~/bin/fleet-up
```

Add `~/bin` to PATH if it isn't already (`echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc`).

After that, the daily ritual is:

```bash
fleet-up                # creates the tmux session with one window per worktree
tmux attach -t fleet    # attach to it
```

Inside each window, launch Claude Code with the role in mind:

```bash
claude                  # starts a session in the current worktree directory
```

Navigate windows with `C-a 1` / `C-a 2` / … or cycle with `C-a n`.
Detach with `C-a d`, reattach later with `tmux attach -t fleet`.

### Persistence across reboots (optional)

tmux sessions die when the WSL distro shuts down. If you want the
fleet layout to survive a Windows reboot, install `tmux-resurrect`
(and optionally `tmux-continuum` for auto-save):

```bash
git clone https://github.com/tmux-plugins/tpm ~/.tmux/plugins/tpm
cat >> ~/.tmux.conf <<'EOF'

# plugins
set -g @plugin 'tmux-plugins/tpm'
set -g @plugin 'tmux-plugins/tmux-resurrect'
set -g @plugin 'tmux-plugins/tmux-continuum'
set -g @continuum-restore 'on'
run '~/.tmux/plugins/tpm/tpm'
EOF
```

Then inside a tmux session press `C-a I` (capital I) to install plugins.
tmux-resurrect will save your session layout and tmux-continuum will
reload it automatically when you run `tmux` again after a reboot.

---

## 6. Model defaults per worktree

In Claude Code, each session can run on a different model. When you
start a session against a worktree, set the model to match the role in
the table above. Inside a running session you can still switch with
`/model`, but starting on the right model avoids burning Opus budget
on routine work.

Practical rule:

- **Opus budget is precious.** Reserve it for the worktrees named
  `opus-*` and for cases where a Sonnet session hits something subtle
  (escalate with `/model opus`).
- **Sonnet is the fleet.** Let sonnet-named worktrees run unattended
  against `TASKS.md` and first-pass reviews. Docs passes, test
  generation, mechanical refactors all go here.

The top-level `CLAUDE.md` "Model split" section has the full rules and
`TASKS.md` uses a `**Model:**` tag on each task.

---

## 7. `settings.json` — permissions and allowlist (optional but helpful)

If you want Sonnet-fleet agents to run unattended overnight, loosen the
default confirmation prompts for commands they need. Edit your Claude
Code settings (not the repo) and add an allowlist for build / test /
`gh` / read-only shell commands. Keep **writes to master, force pushes,
and destructive git commands** on the confirm list.

Rough starting allowlist (all Linux-native, no `cmd.exe` wrappers
needed in WSL):

- `cmake --build ...`
- `cmake --preset linux-debug`
- `ninja ...`, `make ...`
- `gh pr view`, `gh pr diff`, `gh pr list`, `gh pr review --comment`,
  `gh pr create` (but *not* `gh pr merge`)
- `git status`, `git diff`, `git log`, `git fetch`, `git checkout -b`,
  `git add`, `git commit`, `git push -u origin HEAD`
- `ctest ...`
- Common read-only shell: `ls`, `cat`, `head`, `tail`, `wc`, `find`

Leave on prompt:

- `git push origin master`
- `git push --force`
- `git reset --hard`
- `gh pr merge`
- Anything destructive to `.claude/worktrees/`
- `sudo ...` (any use of sudo)
- `apt ...`, `apt-get ...`, `dpkg ...`, package manager anything

---

## 8. `creations/game/` and private repos — decide ownership

Your `creations/game/` (and any other private creation) is gitignored
in the engine repo. You have three options:

1. **Keep private creations fully local.** Agents that want to work on
   them must do so from the main worktree only, because the git
   worktrees won't have the directory. Simplest but limits parallelism
   inside a private creation.
2. **Make each private creation its own git repo.** Nest a separate
   `.git` inside `creations/game/` (or use a submodule). Then the
   private creation has its own worktrees, its own TASKS.md, its own
   `commit-and-push` and `review-pr` skills, its own PR workflow. This
   is the scalable option.
3. **Un-gitignore** `creations/game/` in the engine repo. Not
   recommended — mixes public engine and private game history in one
   repo.

Recommended: **option 2**, with the private repo following the same
parallel-agent pattern (its own `CLAUDE.md`, `TASKS.md`, skills). The
engine-level skills in `.claude/skills/` are already written generically
so they apply inside a private creation's subtree too, but a private
creation can override any of them by shipping its own `.claude/skills/`
under its own directory.

Inside WSL, the private creation clone follows the same filesystem
rule: keep it on the ext4 side (e.g. `~/src/IrredenGame`), not
`/mnt/c`.

---

## 9. Reviewer-worktree workflow

The reviewer agents (`sonnet-reviewer`, `opus-reviewer`) need to
frequently `gh pr checkout <N>`. That requires the reviewer worktree to
be on a throwaway branch so `gh pr checkout` can replace it without
protesting.

Before starting a reviewer session:

```bash
cd ~/src/IrredenEngine/.claude/worktrees/sonnet-reviewer
git checkout -B review-scratch origin/master
```

Then inside the session you can `gh pr checkout 42`, review, post the
comment, and `git checkout -B review-scratch origin/master` again for
the next PR.

Don't commit or push from a reviewer worktree. The `review-pr` skill
enforces this, but a human reviewer should know too.

---

## 10. Configure + build the Linux tree once per boot

Before you start an agent on engine work, prove the Linux build tree is
configured and builds cleanly from the fleet clone. This is the Linux
equivalent of the Windows build-sanity check.

From the fleet clone:

```bash
cd ~/src/IrredenEngine
cmake --preset linux-debug
cmake --build build --target IRShapeDebug -j$(nproc)
```

First-time configure will pull FetchContent deps (GLFW, GLEW, sol2,
RtAudio, RtMidi, fmt, spdlog, easy_profiler, …) into `build/_deps/`.
That takes a minute. Subsequent configures are fast.

### ⚠ Expect Linux-build breakage — this is the point

The engine was originally written for Windows + MSYS2. Getting the
Linux build green is **ongoing work for the fleet** (see the
"Linux-maturation" entry in `TASKS.md`). When something fails to
compile or link, **fix it** rather than working around it — every fix
is forward progress on cross-platform parity.

Common first-time issues you might hit:

- **Missing system libs** — apt the dev package for whatever header is
  missing. Log the package name in `TASKS.md` so the apt list above
  gets expanded.
- **Windows-only preprocessor blocks** — look for `#ifdef _WIN32` /
  `#ifdef _MSC_VER` / `#ifdef __MINGW64__`. Linux probably needs a
  matching branch, not a silent fallthrough.
- **Case-sensitive includes** — Windows filesystems are
  case-insensitive; Linux is not. `#include "IR_Math.hpp"` vs
  `#include "ir_math.hpp"` will bite you.
- **`\r\n` line endings** — Make sure `.gitattributes` or your local
  git config doesn't translate EOLs on checkout into WSL. `git config
  core.autocrlf input` is the right setting inside WSL.

If an agent (Sonnet or Opus) hits a Linux build break mid-task, it
should stop the current task, requeue it, and open a *separate* PR
fixing just the build break. Don't bury the fix inside an unrelated
change — the Linux-maturation work wants to be reviewable on its own.

---

## 11. Running executables inside WSL (and when to run them on Windows instead)

For targets that just need to build successfully, `cmake --build` is
enough. For targets you want to *run* — `IRShapeDebug`, the demos,
anything with a window or audio — WSLg handles the GUI side. Inside a
tmux window:

```bash
cd ~/src/IrredenEngine/build/creations/demos/shape_debug
./IRShapeDebug
```

WSLg routes the GLFW/OpenGL window to a Windows-side surface
automatically. No X server setup needed on Windows 11 / recent Windows
10. Mouse, keyboard, and basic audio work through the same routing.

**Things that don't (yet) work well in WSLg:**

- **MIDI devices.** RtMidi on Linux binds ALSA MIDI, which WSLg
  doesn't forward from Windows-side hardware MIDI. If a demo needs a
  real MIDI controller (`midi_keyboard`, `midi_polyrhythm`), run it
  from the **Windows-native** clone until we find a WSL bridge worth
  setting up.
- **Vsync/timing-sensitive profiling.** WSLg introduces a small
  compositor latency. "Why is this frame 4 ms slower" experiments are
  more trustworthy on the Windows-native build.
- **FFmpeg video recording.** Should work, but verify frame timings
  against the Windows clone before trusting numbers.

This is fine — the *fleet* runs in WSL to build, compile, test, and
iterate. When you need to validate user-facing behavior or do
latency-sensitive work, drop into the Windows clone in VSCode
directly. That's still the authoritative "ship it" environment for now.

---

## 12. Session starter prompts

When kicking off a worktree session in its tmux window, start with a
one-liner that tells the agent which role it's in. Examples:

- **Opus architect session:**
  > "You are the Opus architect agent in `~/src/IrredenEngine/.claude/worktrees/opus-architect`. Pick the next unblocked `[opus]` task from `TASKS.md`, complete it, and open a PR via the `commit-and-push` skill. Build the target you touched with `cmake --build build --target <name> -j$(nproc)` and run the relevant executable before declaring done."

- **Sonnet fleet session:**
  > "You are a Sonnet fleet agent in `~/src/IrredenEngine/.claude/worktrees/sonnet-fleet-1`. Pick the next unblocked `[sonnet]` task from `TASKS.md`, complete it, and open a PR via the `commit-and-push` skill. If the task turns out to touch core engine invariants (rendering, ECS, ownership, concurrency), stop and requeue it as `[opus]` with a note instead of charging ahead. Build and run before declaring done."

- **Sonnet reviewer session:**
  > "You are a Sonnet first-pass reviewer in `~/src/IrredenEngine/.claude/worktrees/sonnet-reviewer`. Review the oldest unreviewed open PR using the `review-pr` skill. Post a first-pass review and end with an explicit escalation line saying whether Opus recheck is required."

- **Opus reviewer session:**
  > "You are the Opus final reviewer in `~/src/IrredenEngine/.claude/worktrees/opus-reviewer`. Review any open PR that has a Sonnet first-pass review flagged for Opus escalation, or any open PR touching core engine invariants. Post a final review and, if approving, use `gh pr review <N> --approve`. Do not merge — I merge."

Save these as Claude Code custom commands (or just paste them) so you
don't retype them each morning.

---

## 13. What *not* to automate

A few things should stay manual even with a fleet running:

- **Merging PRs.** You merge. Agents never call `gh pr merge`.
- **Pushing to `master`.** Agents never `git push origin master`.
- **Force-pushing anywhere.** Agents never `--force`.
- **Deleting worktrees.** Agents never touch `.claude/worktrees/`
  layout.
- **Reconfiguring the build.** Agents never run `cmake --preset` —
  they only `cmake --build` against the already-configured tree.
- **`sudo` anything.** Agents in WSL never use `sudo`. Installing a
  new apt package is a manual step the user does, and then updates
  the apt list in this document.
- **Editing this document.** (Or they can, but you sign off.)

The `commit-and-push` and `review-pr` skills enforce most of this, but
a human check is the last line of defense.

---

## 14. Dry run

Before handing work to the fleet, do one manual end-to-end run with
yourself in the loop:

1. `fleet-up && tmux attach -t fleet`
2. In the `opus-arch` window, run `claude` and ask it to pick the
   example `TASKS.md` item (`benchmark IRShapeDebug at zoom 4`) and
   work through it.
3. Watch the `commit-and-push` skill open a PR.
4. `C-a 4` → `sonnet-rev` window → `claude` → ask it to review the
   PR.
5. `C-a 5` → `opus-rev` window → `claude` → ask it to re-review.
6. Merge manually on GitHub.
7. Back in `opus-arch`, run `start-next-task`, verify the branch
   resets cleanly.

If any of those steps fail, fix the skill — not the task. The point of
the dry run is to uncover workflow bugs, not to complete a task.

---

## Recap

- WSL2 + Ubuntu 24.04 installed and base toolchain apt'd. ✅
- Engine cloned into `~/src/IrredenEngine` (ext4, not `/mnt/c`). ✅
- GitHub CLI installed inside WSL and authed. ✅
- Claude Code installed inside WSL. ✅
- Permanent worktrees created under the WSL clone. ✅
- tmux + `fleet-up` helper ready. ✅
- Model defaults per worktree memorized. ✅
- Optional settings.json allowlist. ✅
- Decision made about `creations/game/` (or other private creations). ✅
- Reviewer worktrees parked on `review-scratch`. ✅
- `cmake --preset linux-debug && cmake --build build` builds cleanly
  from the fleet clone (or Linux-maturation tasks are filed for
  whatever doesn't). ✅
- Session starter prompts saved. ✅
- "What not to automate" understood. ✅
- One dry run through the full loop completed. ✅

Then let the fleet loose.
