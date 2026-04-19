# Parallel-agent workflow: external setup checklist

This is the list of things **you** (the user) need to do once, outside
the repo, to run the parallel-agent workflow described in the top-level
`CLAUDE.md`. Nothing in the repo can set these up for you — they're
environment, auth, and process.

Work through the list top-to-bottom. Each step is independent unless
noted.

## Fleet environment: tmux on WSL2 OR macOS

The parallel-agent fleet runs inside **tmux**, on one of two hosts:

- **WSL2 Ubuntu 24.04 on your Windows PC** — Linux shell, `linux-debug`
  CMake preset, OpenGL backend. Builds in `~/src/IrredenEngine` on the
  WSL ext4 filesystem.
- **macOS native (Apple Silicon or Intel)** — macOS shell, `macos-debug`
  CMake preset, Metal backend. Builds in `~/src/IrredenEngine` on the
  Mac filesystem.

You can run the fleet on either host (or both, for parallel parity
work — see step 11 and the `backend-parity` skill). The two hosts are
independent clones of the engine repo; they share code through
`origin/master` on GitHub.

Why tmux-on-unix either way:

- One shell environment per agent, no `cmd.exe /c "set PATH=...`"
  wrappers around every build command.
- tmux gives a stable, nameable session you can attach/detach from
  multiple terminals and that survives you closing the terminal app.
- Both WSL ext4 and macOS APFS are fast enough to host five concurrent
  CMake builds — unlike `/mnt/c`, which would bottleneck the fleet on
  filesystem I/O.
- Making all three platforms (Linux/WSL, macOS, Windows) work is a
  first-class goal for the engine. Running the fleet from WSL matures
  the Linux path; running it from macOS matures the Metal path; the
  Windows-native clone remains the ship-it authority. See "Cross-
  platform parity" in the top-level `CLAUDE.md` and the `backend-parity`
  skill at `.claude/skills/backend-parity/SKILL.md`.

The existing Windows-native clone at
`C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine` stays — you'll
keep using it for VSCode-native work, validating Windows builds, and
anything that needs the MSYS2 mingw64 toolchain directly. The WSL
clone and the macOS clone are **separate** checkouts with **separate**
build trees; they share history only via `origin/master`.

### Which host for which work

- **Linux / Metal parity.** Use whichever host *lacks* the feature
  you're porting. GLSL → MSL port needs macOS; MSL → GLSL port needs
  WSL or Windows. See the `backend-parity` skill.
- **Routine TASKS.md work.** Either host works, whichever is in front
  of you. Tag the task with `[opus]` / `[sonnet]` and go.
- **Tests, docs, mechanical refactors.** Either host. Sonnet-fleet
  workers on either side.
- **MIDI-dependent demos.** Run from the Windows-native clone — MIDI
  hardware forwarding is unreliable under WSLg and requires extra
  setup on macOS (see step 11).
- **Frame-timing / profiling.** Prefer macOS native or Windows native
  over WSL for timing-sensitive work. WSLg adds a small compositor
  latency that muddies "why is this frame 4 ms slower" experiments.

---

## 1a. WSL2 + Ubuntu + base tooling (Windows host)

*Skip to §1b if you're setting up the fleet on macOS instead.*

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

## 1b. macOS + Homebrew + base tooling (macOS host)

*Skip this section if you're setting up the fleet on WSL instead.*

### Install Xcode Command Line Tools

The Metal backend needs Apple's SDK + `clang++` + `xcrun metal`. The
full Xcode.app isn't required, but the Command Line Tools are:

```bash
xcode-select --install
```

If it's already installed, `xcode-select -p` prints a path like
`/Library/Developer/CommandLineTools` and you can skip this. If you
intend to step-debug Metal shaders in Instruments or use Xcode's
GPU frame capture, install the full Xcode.app too.

### Install Homebrew (if you don't have it)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Follow the post-install prompt to add brew to your shell's PATH
(`/opt/homebrew/bin/brew shellenv` on Apple Silicon, `/usr/local/bin`
on Intel).

### Install the base toolchain

```bash
brew update
brew install \
    cmake ninja pkg-config git \
    tmux \
    ffmpeg \
    clang-format clang-tidy \
    node
```

Notes on what these give you:

- `cmake ninja pkg-config` — build system and config tools. Homebrew
  ships CMake ≥ 3.28, which is what the preset needs.
- `ffmpeg` — provides the development headers + dylibs used by
  `engine/video/`. On macOS the video subsystem finds FFmpeg via
  `pkg-config`, same as on Linux.
- `clang-format clang-tidy` — match the Linux/Windows toolchain so
  the `format` / `format-check` / `lint` targets produce the same
  output across all three hosts.
- `node` — for npm-installing Claude Code later. Skip if you prefer
  nvm (`brew install nvm` and run its post-install instructions).
- The Metal backend itself doesn't need any brew packages; it links
  against Apple's system `Metal.framework`, `MetalKit.framework`,
  `Foundation.framework`, `AppKit.framework`, and `QuartzCore.framework`,
  all of which come with the CLT.

RtAudio / RtMidi are pulled by `FetchContent` and compile against
macOS's CoreAudio / CoreMIDI frameworks directly — no brew packages
required.

### Configure git on the Mac

```bash
git config --global user.name  "Evin Killian"
git config --global user.email "<same email you use for GitHub>"
git config --global init.defaultBranch master
git config --global pull.rebase false
```

### Clone the engine

```bash
mkdir -p ~/src && cd ~/src
git clone git@github.com:jakildev/IrredenEngine.git
cd IrredenEngine
```

`~/src/IrredenEngine` is the canonical fleet clone path on macOS too
(same as WSL). Keeping the path identical means the tmux `fleet-up`
helper script (step 5) works on both hosts without modification.

If you haven't set up an SSH key for GitHub on this Mac yet:

```bash
ssh-keygen -t ed25519 -C "<email>"
pbcopy < ~/.ssh/id_ed25519.pub   # paste into github.com/settings/keys
ssh -T git@github.com            # should greet you
```

---

## 2. GitHub CLI install + auth

Required for the `commit-and-push` and `review-pr` skills.

**On WSL (Ubuntu):** install via the official GitHub apt repo so you
get a current version:

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

**On macOS:** one-liner via Homebrew:

```bash
brew install gh
```

**Both hosts — verify and auth:**

```bash
gh --version
gh auth login
```

Pick **GitHub.com → SSH → Upload existing key (or use the one you just
made) → Login with a web browser**. On WSL the browser opens on the
Windows host via WSLg (transparent on Windows 11 and recent Windows
10). On macOS it opens in your default browser directly. If either
browser flow fails, pick "paste a one-time code" and open the URL
manually.

Scopes you need: `repo`, `read:org`, `workflow`. Verify with:

```bash
gh auth status
```

The output should list those scopes under "Token scopes".

---

## 3. Install Claude Code

On WSL, Claude Code lives inside the WSL distro (not on the Windows
side). On macOS, it lives on the Mac directly. Either way, the install
path is essentially the same:

**Via npm** (simplest if you already have Node):

```bash
# WSL: install Node.js 20 LTS via nvm if you don't have it
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
# (close and reopen your shell so nvm is on PATH)
nvm install 20 && nvm use 20

# macOS: node came from `brew install node` in §1b, so this just works.

# Both hosts: install Claude Code
npm install -g @anthropic-ai/claude-code
claude --version
```

**Via the install script** (if you prefer not to manage Node): follow
whatever install path is current on the Claude Code docs for Linux /
macOS.

From inside any worktree, you start a session with plain `claude`. The
Bash tool inside that session is the host's native Unix shell — no
`cmd.exe` wrapping, no PATH workarounds on either platform.

---

## 4. Permanent worktrees for the agent fleet

The existing `.claude/worktrees/` on the Windows-native clone are
ephemeral, created ad-hoc by the old workflow. For the fleet, create
named worktrees **inside the host clone you're running on**
(`~/src/IrredenEngine` on either WSL or macOS) and reuse them across
sessions. If you plan to run the fleet on both WSL and macOS, each
clone gets its own independent set of worktrees — they don't share.

A reasonable starting setup with the model split in mind:

| Worktree            | Repo   | Model  | Role                                                |
|---------------------|--------|--------|-----------------------------------------------------|
| `opus-architect`    | engine | Opus   | Core engine work, ECS/render/audio. Stand-by.       |
| `opus-worker-1`     | engine | Opus   | Plans `fleet:needs-plan` issues, executes `[opus]` tasks |
| `opus-worker-2`     | engine | Opus   | Second opus-worker pane — parallel planning/execution    |
| `sonnet-fleet-1`    | engine | Sonnet | TASKS.md `[sonnet]` items, tests, docs              |
| `sonnet-reviewer`   | engine | Sonnet | First-pass PR review (polling loop)                 |
| `opus-reviewer`     | engine | Opus   | Final review on flagged PRs (polling loop)          |
| `queue-manager`     | engine | Sonnet | Task intake — categorizes and files new tasks       |
| `game-architect`    | game   | Opus   | Game-side architect / stand-by, cross-repo aware    |

The first seven live in `~/src/IrredenEngine/.claude/worktrees/`. The
last (`game-architect`) lives inside the game repo at
`~/src/IrredenEngine/creations/game/.claude/worktrees/game-architect`,
because the game is its own git repo with its own PR namespace.

You don't have to create these by hand — `fleet-up` (described in §5)
creates any missing worktrees on first run. The manual `git worktree
add` commands below are documented for completeness, in case you want
to bootstrap by hand or recover from a broken state.

Each worktree needs its own seed branch — git refuses to check out
`master` in a second worktree while the main clone still has it
checked out, so we create each worktree on a `fleet/<role>` branch
that starts from `origin/master`. The seed branch is just a parking
spot; `fleet-up` resets it to a fresh `claude/<role>-scratch` branch
on every invocation, and `start-next-task` will move it onto a real
`claude/<area>-<topic>` branch as soon as a task starts.

Manual bootstrap (only if `fleet-up` won't be doing it):

```bash
cd ~/src/IrredenEngine
git fetch origin master

git worktree add -b fleet/opus-architect  .claude/worktrees/opus-architect  origin/master
git worktree add -b fleet/opus-worker-1   .claude/worktrees/opus-worker-1   origin/master
git worktree add -b fleet/opus-worker-2   .claude/worktrees/opus-worker-2   origin/master
git worktree add -b fleet/sonnet-fleet-1  .claude/worktrees/sonnet-fleet-1  origin/master
git worktree add -b fleet/sonnet-reviewer .claude/worktrees/sonnet-reviewer origin/master
git worktree add -b fleet/opus-reviewer   .claude/worktrees/opus-reviewer   origin/master
git worktree add -b fleet/queue-manager   .claude/worktrees/queue-manager   origin/master

cd ~/src/IrredenEngine/creations/game
git fetch origin master
git worktree add -b fleet/game-architect  .claude/worktrees/game-architect  origin/master
```

Verify:

```bash
git worktree list
```

You should see the main clone on `master` plus seven engine worktrees
(`opus-architect`, `opus-worker-1`, `opus-worker-2`, `sonnet-fleet-1`,
`sonnet-reviewer`, `opus-reviewer`, `queue-manager`) each on their own
`fleet/*` seed branch, plus an eighth `game-architect` worktree under
`creations/game/` if the game repo is present. The `fleet/` prefix
keeps these distinct from `claude/<area>-<topic>` agent branches so
`gh pr list` and branch-completion never confuse them.

These worktrees live forever. Each agent session opens the worktree
it wants and the `start-next-task` skill creates a fresh
`claude/<area>-<topic>` branch off `origin/master` inside it after
each PR, so the worktree is reused but the branch name changes each
task. The initial `fleet/<role>` seed branches stay in `git branch`
output as harmless markers of which worktree exists — if you ever
want to remove a worktree entirely, `git worktree remove <path> &&
git branch -D fleet/<role>`.

**Common first-run error:** if you see `fatal: 'master' is already
checked out at '<main clone>'`, you ran `git worktree add <path>
master` (checking out the `master` branch directly) instead of
`git worktree add -b fleet/<role> <path> origin/master` (creating a
new seed branch starting from `origin/master`). Fix by using the
`-b` form above.

You can delete the old ephemeral worktrees (`adoring-gates`,
`goofy-jones`, `vectorized-sauteeing-yeti`) from the Windows-native
clone once this PR merges — they were from the bootstrap phase and have
no persistent role in the fleet.

---

## 5. tmux session layout

tmux is how you keep all the fleet agents visible and
attachable/detachable from one terminal, on either host. The `fleet-up`
script creates **one tmux session** named `fleet` with **one window**
named `agents` containing **seven tiled panes** — one per worktree
role — and auto-launches `claude` in each pane with the matching
**role slash command**.

**Key terminology:** in tmux a *window* fills the whole terminal (only
one shows at a time); a *pane* is a split inside a window. The fleet
uses one window with multiple panes so you can see all agents
simultaneously. The default tmux prefix is `C-b` (Ctrl+B); the config
below remaps it to `C-a` (Ctrl+A) which is easier to reach. **`C-a`
means hold Control, press A.**

On macOS, if you want `pbcopy`/`pbpaste` to work inside tmux, also
`brew install reattach-to-user-namespace` — otherwise the tmux config
below works identically on both hosts.

### Step 5a — `~/.tmux.conf`

First-time setup: drop a minimal `~/.tmux.conf` that makes prefix
`C-a` and gives you mouse scroll:

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

### Step 5b — role slash commands at `~/.claude/commands/`

Each pane in the fleet runs `claude` with a role-specific slash command
as its initial prompt. The role files live at:

- `~/.claude/commands/role-opus-architect.md`
- `~/.claude/commands/role-sonnet-author.md`
- `~/.claude/commands/role-sonnet-reviewer.md`
- `~/.claude/commands/role-opus-reviewer.md`
- `~/.claude/commands/role-queue-manager.md`
- `~/.claude/commands/role-game-architect.md`

Each one is a markdown file with frontmatter that Claude Code
auto-discovers and exposes as a slash command (`/role-opus-architect`,
etc.). Inside Claude, typing the command at the prompt is the same as
the agent receiving that command's body as instructions.

The same files are also checked into the repos for version control:
- engine: `.claude/commands/role-*.md` (everything except game-architect)
- game: `.claude/commands/role-game-architect.md`

Treat the in-repo copies as the source of truth and the user-level
copies (`~/.claude/commands/`) as the runtime cache. `scripts/fleet/install.sh`
(described in §5c) creates `~/.claude/commands/role-*.md` as **symlinks**
into the repo copies — edit the repo file, commit, and every machine
that has run `install.sh` picks up the new version on its next `git pull`.
Re-running `install.sh` is only required when a role file is **added**
or **removed**, not when an existing one is edited.

### Step 5c — `scripts/fleet/install.sh` and `fleet-up`

The fleet launcher and its installer live in the engine repo under
[`scripts/fleet/`](../scripts/fleet/). Two files:

- **`scripts/fleet/fleet-up`** — the launcher. Does four things in
  order:
  1. Ensures all 7 worktrees exist (creates any that are missing).
  2. Resets each worktree to a fresh branch off `origin/master`,
     skipping any that have uncommitted changes.
  3. Creates the tmux session with 7 tiled panes.
  4. In each pane, runs `claude --model <m> "/role-<role> <mode>"`
     with the appropriate model and role. Default mode is `dry-run`;
     pass `live` to skip dry-run.
- **`scripts/fleet/install.sh`** — the one-time per-machine installer.
  Symlinks `scripts/fleet/fleet-up` into `~/bin/fleet-up` and symlinks
  each `.claude/commands/role-*.md` into `~/.claude/commands/`. Picks
  up `creations/game/.claude/commands/role-game-architect.md` too if
  the game repo is cloned.

First-time setup on this machine:

```bash
cd ~/src/IrredenEngine
scripts/fleet/install.sh
```

If the installer warns that `~/bin` is not on PATH, add the line it
prints to your shell startup file (`~/.zprofile` for zsh,
`~/.bash_profile` for bash) and open a new terminal.

Because `install.sh` uses symlinks, a subsequent `git pull` on the
engine repo is enough to update both `fleet-up` and the role slash
commands on every machine — you only need to re-run `install.sh` when
a new role file is **added** to the repo (or an existing one deleted).

### Step 5d — daily ritual

```bash
fleet-up                # creates the session, prepares worktrees, launches claude in each pane
tmux attach -t fleet    # attach to it
```

By default `fleet-up` starts every agent in **dry-run mode**. Each
agent runs its startup actions (fetch, read TASKS, list open PRs,
print a summary) and then **stands by** for explicit human
instruction. This is the safe default — no work happens until you
authorize it.

When you've validated everything looks healthy, promote a pane to its
normal loop by typing in it:

> exit dry-run mode and begin your normal loop

You can also start the whole fleet in live mode from the get-go:

```bash
fleet-up live           # all panes go straight to their normal loop
```

Don't do that until you've successfully run `fleet-up dry-run` end to
end at least once on this machine.

### Tmux navigation cheat sheet

Prefix is `C-a` (Ctrl+A). To send a command, hold Control, press A,
release, then press the next key.

| Binding             | Effect                                            |
|---------------------|---------------------------------------------------|
| `C-a d`             | detach (leaves session running)                   |
| `C-a` then arrow    | move focus between panes                          |
| `C-a o`             | cycle panes                                       |
| `C-a z`             | zoom/unzoom current pane (fullscreen toggle)      |
| `C-a space`         | cycle pane layouts (tiled / even-horizontal / …)  |
| `C-a %`             | split pane vertical                               |
| `C-a "`             | split pane horizontal                             |
| `C-a {` / `C-a }`   | swap current pane with prev/next                  |
| `C-a x`             | kill current pane (confirms)                      |
| `C-a [`             | enter scroll/copy mode (`q` to exit)              |
| `C-a ?`             | list all bindings                                 |
| mouse               | click focuses, drag borders resizes, wheel scrolls |

Reattach after a detach: `tmux attach -t fleet`.

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

Rough starting allowlist (works identically on WSL and macOS — plain
Unix commands, no `cmd.exe` wrappers):

- `cmake --build ...`
- `cmake --preset linux-debug` (WSL) / `cmake --preset macos-debug` (Mac)
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
- `apt ...`, `apt-get ...`, `dpkg ...` (WSL package manager)
- `brew install ...`, `brew upgrade ...`, `brew uninstall ...` (macOS)
- `xcode-select --install`, `softwareupdate ...`

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

On either WSL or macOS, keep private-creation clones on the host's
native filesystem (e.g. `~/src/IrredenGame`). On WSL specifically,
never use `/mnt/c` — I/O there is slow enough to bottleneck the
fleet.

---

## 9. Reviewer-worktree workflow

The reviewer agents (`sonnet-reviewer`, `opus-reviewer`) need to
frequently `gh pr checkout <N>`. That requires the reviewer worktree to
be on a throwaway branch so `gh pr checkout` can replace it without
protesting.

### Basic setup

Before starting a reviewer session:

```bash
cd ~/src/IrredenEngine/.claude/worktrees/sonnet-reviewer
git checkout -B claude/sonnet-reviewer-scratch origin/master
```

Then inside the session you can `gh pr checkout 42`, review, post the
comment, and `git checkout -B claude/sonnet-reviewer-scratch origin/master`
again for the next PR.

Don't commit or push from a reviewer worktree. The `review-pr` skill
enforces this, but a human reviewer should know too.

### Reviewer-loop pattern (poll-and-review while you're offline)

The whole point of a fleet is that author and reviewer agents can have
a back-and-forth while you're AFK. That requires the reviewer agent to
keep checking for new PRs on its own, not wait for you to type "review
PR 42" each time.

**Primary approach — `/loop` via `fleet-babysit` (what the fleet uses).**
`fleet-up` launches each reviewer with `fleet-babysit` and a loop
interval. In live mode, `fleet-babysit` wraps the role invocation in
Claude's built-in `/loop` skill:

```text
# What fleet-babysit runs under the hood:
claude --model sonnet "/loop 10m /role-sonnet-reviewer"
claude --model opus  "/loop 30m /role-opus-reviewer"
```

Each `/loop` fire re-invokes the full role — the agent fetches PR
lists, reviews candidates, and exits cleanly. The `/loop` driver
handles the interval between fires. `fleet-babysit` handles crash
recovery and rate-limit backoff around the outer session.

Benefits over the old self-managed sleep and tmux-timer approaches:
- Consistent scheduling across all polling agents
- Claude-runtime-managed intervals (survives session compaction)
- Built-in rate-limit awareness
- Easy to pause (`/loop stop`) when you come back

The queue-manager uses the same pattern at a 15-minute interval.

**Alternative — `scheduled-tasks` / `schedule` skill (most robust).** For
"keep running even if every tmux session is dead and my laptop slept,"
create a scheduled remote trigger that fires every 15 minutes and
re-runs a small review-new-PRs prompt. Unlike `/loop`, this runs
independently of any running Claude Code window — it spawns a fresh
Claude run on each fire. Downside: each fire has no memory of the
previous fire, so the agent has to re-derive "what have I already
reviewed" from the GitHub state every time (cheap, but not free).

Use the `schedule` skill to create it:

```text
Schedule a task to run every 15 minutes:
"On github.com/jakildev/IrredenEngine, run `gh pr list --state open`,
and for every PR that does not yet have a review authored by
<reviewer github username>, invoke the `review-pr` skill on it. Then
exit."
```

Layer this on top of the `/loop` approach if you want redundancy: the
tmux reviewer window handles the common case with low latency; the
scheduled task is the safety net that catches everything if the tmux
window dies.

### Two-tier review with both reviewers running

The Opus/Sonnet model split (see root `CLAUDE.md`) wants most first-
pass reviews to be Sonnet and only have Opus look at core-engine PRs
or Sonnet-escalated ones. In the reviewer-loop pattern that means:

- `sonnet-reviewer` uses `/loop 10m`, reviews **every** open
  unreviewed PR at Sonnet cost, and writes a verdict. If the PR
  touches `engine/render/`, `engine/entity/`, `engine/system/`,
  `engine/world/`, `engine/audio/`, `engine/video/`, or
  `engine/math/`, or the Sonnet verdict ends with an Opus-escalation
  line, the review body explicitly says "please Opus-recheck."
- `opus-reviewer` uses `/loop 30m` and filters for PRs whose latest
  Sonnet review asked for an Opus recheck. It reads the Sonnet review
  first, then focuses on what Sonnet couldn't confirm — concurrency,
  lifetime, ECS invariants three systems deep.

This keeps most reviews on Sonnet (cheap) while guaranteeing core
invariants still get an Opus pass. When Opus budget is tight, you
can safely disable the `opus-reviewer` loop for a while — the
escalation notes accumulate in the PR comments and you can come back
and drain them manually or by enabling the loop again.

### Author-side response loop (close the back-and-forth)

The complement to a polling reviewer is a polling author. Each
`sonnet-1` / `sonnet-2` window can be launched with a prompt like:

```text
You are a persistent task runner for the Irreden Engine fleet. Your
workflow is:

1. If any PR you previously opened has new review comments you have
   not yet addressed, read the comments and fix them. Use
   `commit-and-push` to push the fix. Request re-review via
   `gh pr comment <N> --body "re-review please"`.
2. Otherwise, pick the next unblocked `[sonnet]`-tagged task from
   `TASKS.md`, work it, and use `commit-and-push` when done.
3. After `commit-and-push`, use `start-next-task` to land on a fresh
   branch before step 1 repeats.
4. If you hit a usage-limit error, wait until the stated reset time
   and resume.
```

With both the reviewer loop and the author loop running, an offline
round trip looks like: author opens PR → Sonnet reviewer posts needs-
fix within ~10 min → Opus reviewer escalates within ~30 min if core
is touched → author sees the review comments on its next poll, fixes,
re-pushes → reviewer re-reviews → either approve (waiting for you to
merge) or another round. You come back, look at what converged, merge
the clean ones, pick apart the stuck ones.

---

## 10. Configure + build the host's tree once per boot

Before you start an agent on engine work, prove the build tree is
configured and builds cleanly from the fleet clone on the host you're
using.

**On WSL:**

```bash
cd ~/src/IrredenEngine
cmake --preset linux-debug
cmake --build build --target IRShapeDebug -j$(nproc)
```

**On macOS:**

```bash
cd ~/src/IrredenEngine
cmake --preset macos-debug
cmake --build build --target IRShapeDebug -j$(sysctl -n hw.ncpu)
```

First-time configure will pull `FetchContent` deps (GLFW, GLEW on
Linux, sol2, RtAudio, RtMidi, fmt, spdlog, easy_profiler, …) into
`build/_deps/`. That takes a minute. Subsequent configures are fast.

### ⚠ Expect breakage — this is the point

The engine was originally written for Windows + MSYS2. Both the
Linux-on-WSL path and the macOS-Metal path are **still being
matured**. When something fails to compile, link, or run, **fix it**
rather than working around it — every fix is forward progress on
cross-platform parity.

Common first-time issues you might hit:

**On WSL (Linux/OpenGL):**

- **Missing system libs** — apt the dev package for whatever header is
  missing. Log the package name in `TASKS.md` so the apt list in §1a
  gets expanded.
- **Windows-only preprocessor blocks** — look for `#ifdef _WIN32` /
  `#ifdef _MSC_VER` / `#ifdef __MINGW64__`. Linux probably needs a
  matching branch, not a silent fallthrough.
- **Case-sensitive includes** — Windows filesystems are
  case-insensitive; Linux is not. `#include "IR_Math.hpp"` vs
  `#include "ir_math.hpp"` will bite you.
- **`\r\n` line endings** — make sure `.gitattributes` or your local
  git config doesn't translate EOLs on checkout into WSL. `git config
  core.autocrlf input` is the right setting inside WSL.

**On macOS (Metal):**

- **Objective-C++ build flags** — `metal_cocoa_bridge.mm` needs the
  Objective-C++ mode. If the build complains about ARC or missing
  frameworks, check that the target links `Foundation`, `AppKit`,
  `QuartzCore`, `Metal`, and `MetalKit` (see
  `engine/render/CMakeLists.txt`).
- **Shader parity gaps** — the `macos-debug` build ships Metal
  versions of most but not all shaders. When a demo links against a
  missing shader, that's a `backend-parity` skill job. Keep a list
  and run the skill after the build is otherwise green.
- **FFmpeg / fmt header shadowing** — `brew install ffmpeg` installs
  headers under `/opt/homebrew/include/` (Apple Silicon), which also
  contains Homebrew's own `fmt`. Earlier versions of the engine put
  this broad prefix first in the hint list, causing Homebrew `fmt 12.x`
  to shadow the FetchContent-fetched `fmt 10.1.1` that spdlog depends
  on, and failing compilation with "no template named
  `basic_format_string` in namespace `fmt`". Fixed in the engine:
  `engine/video/CMakeLists.txt` now prefers the keg-only path
  `/opt/homebrew/opt/ffmpeg/include` (FFmpeg-only headers) first.
  Detection is automatic; no `PKG_CONFIG_PATH` export needed. If
  detection still fails, set `IRREDEN_FFMPEG_ROOT=/opt/homebrew/opt/ffmpeg`
  (Apple Silicon) or `IRREDEN_FFMPEG_ROOT=/usr/local/opt/ffmpeg` (Intel)
  as a CMake cache variable.
- **Apple Silicon vs Intel** — the Metal backend should work on
  both, but if you're on an older Intel Mac and hit Metal 3-only
  features, flag it. The engine targets Metal 3+.

If an agent (Sonnet or Opus) hits a build break mid-task, it should
stop the current task, requeue it, and open a **separate** PR fixing
just the build break. Don't bury the fix inside an unrelated change —
the cross-platform maturation work wants to be reviewable on its own.

---

## 11. Running executables on each host

For targets that just need to build successfully, `cmake --build` is
enough. For targets you want to *run* — `IRShapeDebug`, the demos,
anything with a window or audio — the two hosts behave slightly
differently.

### On WSL

WSLg handles the GUI side. Inside a tmux window:

```bash
cd ~/src/IrredenEngine/build/creations/demos/shape_debug
./IRShapeDebug
```

WSLg routes the GLFW/OpenGL window to a Windows-side surface
automatically. No X server setup needed on Windows 11 / recent Windows
10. Mouse, keyboard, and basic audio work through the same routing.

**Things that don't (yet) work well on WSLg:**

- **MIDI devices.** RtMidi on Linux binds ALSA MIDI, which WSLg
  doesn't forward from Windows-side hardware MIDI. If a demo needs a
  real MIDI controller (`midi_keyboard`, `midi_polyrhythm`), run it
  from the **Windows-native** clone or from the **macOS** clone
  (CoreMIDI + `brew install` nothing — it just works) until a WSL
  bridge is set up.
- **Vsync / timing-sensitive profiling.** WSLg introduces a small
  compositor latency. "Why is this frame 4 ms slower" experiments are
  more trustworthy on the Windows-native or macOS-native build.
- **FFmpeg video recording.** Should work, but verify frame timings
  against the Windows or macOS clone before trusting numbers.

### On macOS

Native Cocoa window, native Metal rendering, native CoreAudio and
CoreMIDI. Just run the binary:

```bash
cd ~/src/IrredenEngine/build/creations/demos/shape_debug
./IRShapeDebug
```

**Things to watch on macOS:**

- **Metal shader parity.** If a demo crashes at launch with a missing
  pipeline or library-load error, that's usually a Metal-side parity
  gap. Run the `backend-parity` skill to port the missing piece.
  See `.claude/skills/backend-parity/SKILL.md`.
- **Gatekeeper / notarization.** For local dev binaries this isn't
  an issue, but if you see "cannot be opened because the developer
  cannot be verified" on a freshly-built exe, `xattr -dr
  com.apple.quarantine <path>` clears it (or check System Settings →
  Privacy & Security).
- **`hw_display_scale`.** Retina scaling on Mac may hit assumptions in
  the iso-math around pixel sizes — file these as Metal-maturation
  tasks when you find them.
- **Frame timing is dead honest.** macOS + Metal is probably the
  cleanest host you have for profiling work right now. Treat timing
  numbers from the Mac as the reference.

### Cross-platform parity workflow

This is the main reason to have both fleets running. After a render
PR lands on master that touched only one backend:

1. On the host that matches the **lagging** backend, open a Claude
   Code session in any worktree.
2. Ask it to run the `backend-parity` skill with the PR number.
3. The skill audits the diff, ports the other backend, builds and
   smoke-runs the demo, and opens a parity PR.
4. The reviewer agent on the same host (or the other one) reviews
   it.

See the `backend-parity` skill for the full flow and the shader
translation cheatsheet.

---

## 12. Role slash commands (replaces the old paste-prompts ritual)

Each fleet pane runs `claude` with a **role slash command** as its
initial prompt. The role files at `~/.claude/commands/role-*.md`
define what each agent does, in much more detail than a one-line
prompt — they include startup actions, the loop the agent runs, the
escalation rules, the hard rules, and a `dry-run` mode.

You don't need to paste prompts into each pane every morning anymore.
`fleet-up` does it for you: each pane is launched with
`claude --model <m> "/role-<role> dry-run"` baked in. The role files
are the source of truth for agent behavior; if an agent is doing the
wrong thing repeatedly, edit its role file once and the change applies
to every future fleet-up.

### Available roles

| Slash command              | Model  | Worktree           | Loop?         |
|----------------------------|--------|--------------------|---------------|
| `/role-opus-architect`     | Opus   | `opus-architect`   | Stand-by      |
| `/role-sonnet-author`      | Sonnet | `sonnet-fleet-*`   | Continuous    |
| `/role-sonnet-reviewer`    | Sonnet | `sonnet-reviewer`  | Polling 10min |
| `/role-opus-reviewer`      | Opus   | `opus-reviewer`    | Polling 30min |
| `/role-queue-manager`      | Sonnet | `queue-manager`    | On demand     |
| `/role-game-architect`     | Opus   | `game-architect`   | Stand-by      |

Each command takes one optional argument: `dry-run` or `live`.
`dry-run` means "do startup actions and then stop." `live` (or no
argument when invoked from inside an interactive session) means "run
your normal loop."

### Role file shape (for editing)

The role files are markdown with frontmatter:

```markdown
---
description: <short description shown in /help>
---

You are the <role> for the fleet, running on macOS in <worktree>.

Mode (optional argument): $ARGUMENTS

## Responsibilities
...

## Startup actions
...

## Loop behavior
...

## Hard rules
...
```

`$ARGUMENTS` is substituted with whatever was passed to the slash
command — that's how `dry-run` vs `live` flows through.

To edit a role's behavior:

```bash
$EDITOR ~/src/IrredenEngine/.claude/commands/role-sonnet-author.md
# then mirror to runtime location:
cp ~/src/IrredenEngine/.claude/commands/role-sonnet-author.md \
   ~/.claude/commands/
```

The next `fleet-up` (or any new `claude "/role-..."` invocation) picks
up the change.

### Inserting a task while the fleet is running

If you want to add a task to the queue while agents are working:

1. Switch to the `queue-manager` pane (`C-a` then click, or `C-a` then
   arrow keys).
2. Type a rough description of the task. Don't worry about format,
   tags, or repo — just describe what you want done.
3. The queue-manager will categorize (engine vs game), tag (`[opus]`
   vs `[sonnet]`), pick an Area, draft the entry using the TASKS.md
   template, and open a queue-update PR. Paste the PR URL.
4. Once the queue PR merges, the next sonnet-fleet pane on its
   `start-next-task` cycle will see the new task and pick it up
   automatically.

The queue-manager will push back if your description is missing a
concrete Acceptance criterion. That's intentional — tasks without
acceptance checks are tasks that get half-finished.

### Backend-parity sessions (still ad-hoc, not a fleet pane)

`backend-parity` work isn't a permanent role — it's run on demand on
whichever host is lagging. From any worktree on the lagging host,
just type:

> Run the backend-parity skill. <pick a feature, PR range, or "audit">

The skill flow is documented in `.claude/skills/backend-parity/SKILL.md`.

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

The `commit-and-push` and `review-pr` skills document most of this as
hard-rule anti-patterns, and the role files reinforce them, but a human
check is the last line of defense.

---

## 14. Dry run — first time bringing up the fleet

Before handing real work to the fleet, do one manual end-to-end run
with yourself in the loop. The point is to uncover workflow bugs in
the role files, the skills, or the permissions setup — not to
complete an actual task. Anything that breaks here is a real fleet
bug worth fixing before unattended runs.

### Step 1 — bring the fleet up in dry-run mode

```bash
fleet-up                # defaults to dry-run mode
tmux attach -t fleet
```

Each pane will:
1. Start `claude` with the right model.
2. Auto-execute its `/role-<x> dry-run` command.
3. The agent does its startup actions: `git fetch`, reads `TASKS.md`,
   runs `gh pr list`, prints a one-line summary.
4. The agent prints `<role> standing by (dry-run)` and waits.

Walk through every pane (`C-a` then arrow, or click) and confirm
each one printed its standing-by line. If any pane shows an error
instead, that's the first thing to fix.

### Step 2 — drive one author task through the full cycle

Switch to **sonnet-fleet-1** and type:

> exit dry-run mode and do exactly ONE task end-to-end. Pick the
> "Example: unit tests for engine/math/physics.hpp" task — it's
> bounded, sonnet-tagged, and has a concrete acceptance check.
> Build with `-j$(nproc 2>/dev/null || sysctl -n hw.ncpu)`. After
> `commit-and-push`, stop and wait — do not pick another task or loop.

Watch the agent:
- Cross-check `gh pr list` (it should mention this in its picking step).
- Flip the task to `[~]` in `TASKS.md` and commit.
- Read `engine/math/CLAUDE.md` for the helper specs.
- Write the test file, build the test target, run it.
- Call `commit-and-push`. A PR opens.
- Stop and print the PR URL.

If the agent prompts for permission on commands that should be in
your allowlist, **note them down** — they go into `~/.claude/settings.json`
(or your `permissions.allow` list) for next time. Don't fix them mid-run;
finish the dry run first.

### Step 3 — watch the Sonnet reviewer pick it up

Switch to **sonnet-reviewer** and type:

> exit dry-run mode and review exactly ONE PR — the one sonnet-fleet-1
> just opened. Use the `review-pr` skill. End with the explicit
> "Opus recheck not required" or "Opus recheck required" line. Stop
> after the review posts.

Watch the agent:
- `gh pr checkout` the new PR.
- Read the diff in context.
- Post a structured review via `review-pr`.
- End with the recheck-line. For physics tests it should be
  "Opus recheck not required" — physics.hpp is math-only and touches no
  invariants.

### Step 4 — Opus reviewer stays idle (no escalation)

Switch to **opus-reviewer**. It should still be standing by — the
Sonnet review didn't flag for Opus recheck, so there's nothing for
Opus to do. Confirm the pane is healthy and waiting.

### Step 5 — you merge

From a separate terminal (or any pane that isn't running an agent —
`C-a c` to open a fresh window inside the fleet session):

```bash
gh pr merge <N> --squash
```

Or merge through the GitHub web UI. **Agents never merge.**

### Step 6 — test `start-next-task`

Switch back to **sonnet-fleet-1** and type:

> run the `start-next-task` skill

It should rebase onto the merged master, land on a fresh `claude/...`
branch, and the `[x]` Done move from the merged PR should now be
visible in `TASKS.md`.

### Step 7 — test the queue-manager

Switch to **queue-manager** and type a rough task:

> add a task: I want exhaustive tests for engine/common/ir_constants.hpp.
> Sonnet should be able to handle this. Acceptance is "test binary
> builds and all assertions pass".

Watch the queue-manager:
- Categorize as engine repo, `[sonnet]`, area `engine/common`.
- Format the task using the template.
- Append to `TASKS.md`.
- Call `commit-and-push` with a `queue: add task ...` PR.
- Paste the PR URL.

This validates the task-intake flow. You can merge that queue PR
or close it — the point was to see the flow work, not to land the
task.

### Step 8 — test the game-architect (cross-repo awareness)

Switch to **game-architect** and type:

> read the engine TASKS.md and the game TASKS.md, then describe a
> hypothetical game task that would require an engine change. Don't
> file it — just walk me through what the cross-repo flow would look
> like.

The game-arch should:
- Reference the cross-repo escalation flow from the game `CLAUDE.md`.
- Explain that it would file the engine task FIRST, then the blocked
  game task with `Blocked by:` pointing at the engine PR URL.
- NOT actually file anything.

This validates the agent has read the game `CLAUDE.md` correctly.

### What to look for

- Did `commit-and-push` open a PR cleanly without permission prompts?
- Did `review-pr` check out the PR, post the review, and detach cleanly?
- Did the `[~]` → `[x]` move land in the merged commit, and did the
  Done list pick it up?
- Did `gh pr list` show the claim quickly enough that
  `opus-worker-2` would not have picked the same task?
  (Test: while opus-worker-1 is mid-task, switch to opus-worker-2
  and ask it to "list candidate tasks." It should NOT include the
  in-flight task.)
- Did the build go cleanly with
  `-j$(nproc 2>/dev/null || sysctl -n hw.ncpu)`? On macOS,
  hitting an Objective-C++ flag, missing Metal shader, or FFmpeg
  `pkg-config` issue is realistic on first run; on Linux/WSL, expect
  case-sensitive include drift, missing `#include` for headers Windows
  was transitively pulling in, and EOL trouble. Either way, when it
  happens, file the build break as its own task per §10 rather than
  working around it.

### After the dry run

If everything went clean, you can promote the fleet to live mode:

```bash
tmux kill-session -t fleet
fleet-up live
tmux attach -t fleet
```

All panes will start their normal continuous loops immediately. You
can then walk away (or watch) and let the fleet drain `TASKS.md`
overnight.

If something broke, fix the role file, the skill, or the permissions
list — and run the dry run again. Don't go to `fleet-up live` until
the dry run passes end-to-end.

---

## 15. Token exhaustion and recovery

You will hit your Anthropic subscription usage cap during fleet runs,
especially on Opus windows. Here's what actually happens and how to
make the fleet survive it.

### What happens when a window hits its cap

- **The in-flight turn fails.** The API returns a usage-limit error,
  Claude Code surfaces it in the tmux pane, and the current response
  ends partway through. Tool calls that already ran stay run — files
  written are written, commits made are made — so the filesystem is
  never in a half-state you can't recover from.
- **The session state is preserved on disk.** The conversation
  transcript lives in `~/.claude/projects/<project-slug>/<session-id>.jsonl`
  regardless of how the turn ended. You don't lose the conversation,
  the todo list, or the context window. `claude --continue` /
  `claude --resume <id>` re-enters the exact session after the reset.
- **The other models keep working.** Opus and Sonnet budgets are
  separate. When Opus is capped, every Sonnet window in the fleet
  keeps going and vice versa. This is the single biggest reason the
  root `CLAUDE.md` wants you to tag tasks `[opus]` or `[sonnet]` — it
  turns the budget split into a backpressure signal instead of a
  fleet-wide stall.
- **Resumption is manual by default.** I don't think Claude Code
  auto-retries the failed request at reset time — the pane just sits
  on the error until you re-prompt. If you want automatic resumption,
  you need to wrap the agent invocation in a retry loop (below).

### Defensive practices that pay off here

- **Commit early, commit often.** Agents should call `commit-and-push`
  at every logical boundary, not "when the task feels done." If a
  Sonnet window hits the cap mid-refactor and you only re-enter it
  hours later, you want its last good state already on a pushed
  branch, not stuck in an unsaved edit buffer.
- **Author-side response loops should handle the error explicitly.**
  The launch prompt for `sonnet-1` / `sonnet-2` (see §9, "Author-side
  response loop") already tells the agent to wait until the stated
  reset time and resume. Keep that line in every persistent-agent
  prompt — it's the difference between a window that comes back on
  its own and one that sits dead until you attach.
- **Keep `TASKS.md` in git.** It already is. The effect is that if a
  window dies permanently, the next window picking up from `TASKS.md`
  knows where to start — no state is stranded in one dead agent.
- **Watch Opus usage.** Opus eats budget much faster than Sonnet per
  dollar. If you find yourself running two Opus windows in parallel
  on core-engine work, ask whether one of them can demote to Sonnet
  with an Opus final-pass review. The `[opus]` / `[sonnet]` tags are
  the control knob — re-tag aggressively when you see Opus budget
  dropping.
- **Sonnet-first review.** As above in §9, every PR gets a Sonnet
  first-pass review cheaply; Opus only looks at core-engine PRs or
  ones where the Sonnet review asked for escalation. Skipping the
  Sonnet pass and going straight to Opus is the fastest way to burn
  through Opus budget on style nits.
- **Reviewer windows are safe to pause.** If you see Opus budget
  cratering, disable the `opus-rev` loop window first — Sonnet first-
  pass reviews still run, escalation notes accumulate as PR comments,
  and you can drain them manually or re-enable Opus review later. No
  reviewer work is lost, it just waits.

### Shell-level retry wrapper (optional but robust)

If you want a window to come back on its own after a usage-limit
stall, wrap the `claude` invocation in a loop that retries at a
reasonable interval:

```bash
# ~/bin/claude-persistent <session-name> <launch-prompt-file>
# Keeps a Claude Code session alive across usage-limit stalls.
#!/usr/bin/env bash
set -u
session="$1"
prompt_file="$2"
while true; do
    claude --session-name "$session" --input-file "$prompt_file"
    rc=$?
    case "$rc" in
        0) break ;;                 # agent exited cleanly, stop
        2) sleep 300 ;;              # usage-limit-ish error, wait 5 min
        *) sleep 30 ;;               # other error, shorter backoff
    esac
done
```

(Exact exit codes and flags depend on your Claude Code version — treat
the above as a template, not copy-paste.) Launch it from a tmux window
instead of calling `claude` directly, and the window will self-heal
after resets. Pair it with `tmux-resurrect` so the window survives
reboots too.

### When the error persists after reset

If a window keeps hitting the cap even after the stated reset time:

- Check whether the Mac and the WSL host are both running Opus windows
  against the same account — they share a budget, so "two hosts" does
  not mean "two budgets."
- Check whether a background `loop` or scheduled task is quietly
  burning budget in parallel — `schedule list` / whatever the
  equivalent is on your setup.
- Consider demoting one or more windows to Sonnet temporarily. Tag
  the in-flight task in `TASKS.md` with `[sonnet]` so the agent knows
  it has been demoted and doesn't reach for Opus-only reasoning.

### When a window dies entirely

Each agent is wrapped in `fleet-babysit`, which auto-resumes the
session on crash, usage limit, or clean exit. Crash diagnostics are
logged to `~/.fleet/logs/<role>.log`. If an agent hits the 200-attempt
safety limit, an alert file is written to `~/.fleet/logs/<role>.alert`.

If you need to investigate a crash manually:

1. Check `~/.fleet/logs/<role>.log` for exit codes and timestamps.
2. Check the JSONL transcript at
   `~/.claude/projects/<slug>/<session>.jsonl` — it should still be
   there.
3. Start a new `claude` in the same worktree, pass `--resume <session-id>`,
   and you land back in the same conversation with the same context
   window.

No work is ever lost as long as `commit-and-push` was run at the last
logical boundary. That's the whole reason the skill exists.

---

## Recap

**On each host you intend to run the fleet on (WSL and/or macOS):**

- Host base toolchain installed (§1a for WSL, §1b for macOS). ✅
- Engine cloned into `~/src/IrredenEngine` (on the host's native
  filesystem). ✅
- GitHub CLI installed on this host and authed. ✅
- Claude Code installed on this host. ✅
- Permanent worktrees created under this host's clone. ✅
- tmux + `fleet-up` helper ready. ✅
- Build tree configured with the right preset
  (`linux-debug` on WSL / `macos-debug` on macOS) and `IRShapeDebug`
  builds cleanly (or cross-platform-maturation tasks are filed for
  whatever doesn't). ✅
- Reviewer worktrees parked on `claude/<role>-scratch`. ✅

**Shared across hosts:**

- Model defaults per worktree memorized. ✅
- Optional settings allowlist in place. ✅
- Decision made about `creations/game/` (or other private creations). ✅
- Session starter prompts saved, including the reviewer-loop launch
  prompt (§9) for at least one reviewer window. ✅
- "What not to automate" understood. ✅
- `backend-parity` skill documented and known — you know to invoke it
  after any render PR that touched only one backend. ✅
- Token-exhaustion recovery plan understood (§15): which windows
  self-heal, which sit dead, which pause first when Opus budget
  craters. ✅
- One dry run through the full loop completed on at least one host. ✅

Then let the fleet loose.
