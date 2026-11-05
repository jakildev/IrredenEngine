# Command actions and persistent keymaps

Status: implemented. Owner module: `engine/command/`; widget and window surface in
`engine/prefabs/irreden/render/`.

## Problem

A creation's operations were reachable three ways that did not know about each
other: a key bound with `createCommand`, a widget's click handler, and a script.
A user could not find an operation by name or by key, could not run one without
a key, could not change its key at runtime, and could not keep a change across a
restart. Default-binding manifests (`registerBindings`) only rewrite keys before
registration.

## Model

An **action** is one operation with a stable identity:

| Field | Meaning |
|---|---|
| `id_` | Namespaced, lowercase (`editor.tool.erase`). The keymap key; never a display string. |
| `label_`, `section_`, `description_` | What the Actions window lists and searches. |
| `context_` | `global` (default) or a named context the creation turns on and off. |
| `defaultChords_` | Zero or more keyboard chords. Zero is legal: an action never needs a key. |
| `invoke_` | The operation. Keys, widget clicks, `invokeAction` and Lua all call it. |
| `release_` | Optional. Makes the action a hold: `invoke_` starts, `release_` ends. |
| `available_` | Optional predicate. An unavailable action neither fires from a key nor runs by id. |

A **chord** is a key plus the exact modifier set (`ctrl+K` does not fire under
`ctrl+shift+K`, and `K` does not fire while a modifier is held).

## Dispatch

`ActionRegistry` lives inside `CommandManager` and gives each chord ordinary
button rows, so text capture (`setKeyboardCaptured`), modifier specificity and
start/end pairing apply to actions exactly as to `createCommand` bindings.

- **Rebind** moves the chord's rows in place (`rebindUserCommand`); **unbind**
  parks them (inactive rows are skipped by dispatch, by grouping and by
  `isButtonBound`). No `CommandId` is invalidated and the per-frame loop carries
  no tombstones; parked rows are reused by later chords of the same action.
- **Holds** end through a latch-release row: it fires on the key's release
  whatever modifiers are held or captured, and the action's own latch decides
  whether a hold is live. Any binding change ends a live hold first, so a rebind
  during a held press balances its release exactly once.
- **Conflicts** are scoped by context: two actions may share a chord when their
  contexts are disjoint; a `global` action overlaps every context. A plain
  `createCommand` row that would fire alongside the chord is a conflict that
  cannot be displaced (a bare row shadowed by the chord's specificity is not).
  `ConflictPolicy::REPLACE` takes the chord from the other actions.
- **Adoption.** `adoptNamedCommands(prefix)` turns a creation's named
  `createCommand` bindings into actions without touching their call sites. Each
  adopted chord keeps firing its own body with its original blocked mask until
  the user reassigns it; same-named bindings become one action; start/end pairs
  stay plain. The caller's `CommandId` still fires the body.

## Persistence

`KeymapStore` ties the registry to `<root>/keymaps/<creation>.keymap`, where
`<root>` is `$IR_PREFS_DIR` or `IRUtility::userDataDir("irreden")`. It is
separate from any project or document.

```
irreden-keymap 1
creation voxel_editor
bind editor.tool.erase U
unbind editor.class_paint
```

- Only customized actions are written, sorted by id; an action with no line uses
  its defaults, so a reordered registration changes nothing.
- An id with no registered action is kept and written back, so disabling an
  extension does not lose its bindings.
- Saves write `<file>.tmp` and rename it over the file; a failed write leaves
  the previous file and the session's bindings.
- A file whose header is not a keymap, or whose version is newer, is not applied
  and is never overwritten for the session. Unparseable statement lines are
  skipped and counted.
- A scripted run (`--auto-screenshot`, a GUI test) touches the keymap only when
  `IR_PREFS_DIR` is set, so a replay never reads or writes a person's bindings.

## Widgets and the Actions window

`IRPrefab::Widget::bindAction(widget, id)` gives a control a keyless
`C_WidgetHotkey` whose hint is the action's first chord; `WIDGET_SYNC_ACTION_HINTS`
rewrites it when the registry generation changes, so the Alt badge and the list
agree. `registerFocusAction` / `registerStepAction` resolve their field when
they run, so a rebuilt panel keeps working once it rebinds.

The Actions window (`actions_window.hpp`, `System<ACTIONS_WINDOW>`) searches by
name, id, section, description and chord text; runs the selected action;
assigns, adds or removes a chord by capturing the next key press while a capture
field holds keyboard focus; offers REPLACE / CANCEL on a conflict; and restores
defaults. Its widgets paint in the shared widget passes, so a creation places it
over GUI space no other widget occupies.

## Out of scope

Macros, multi-step key sequences, MIDI learn, gamepad chords and mouse chords.
MIDI and gamepad command dispatch is unchanged.
