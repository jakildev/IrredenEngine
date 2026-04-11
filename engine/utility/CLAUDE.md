# engine/utility/ — file and path helpers

Minimal header-only utilities. No classes, no state — just a few free
functions for reading files and composing paths. Used by `asset/`,
`video/`, and creation startup code.

## Entry point

`engine/utility/ir_utility.hpp` — umbrella header that pulls in
`file_utils.hpp` and `path_utils.hpp`.

## What's here

- `IRUtility::readFileAsString(filepath)` — load a text file into a
  `std::string`. No encoding normalization. Throws on failure.
- `IRUtility::joinPath(dir, filename, ext)` — concatenate a directory,
  filename, and extension. Handles separator insertion.
- `IRUtility::pathWithExtension(path, ext)` — replace or append an
  extension.
- `IRUtility::formatNumberedFilename(prefix, index, width, ext)` — e.g.
  `("screenshot", 7, 4, "png") → "screenshot_0007.png"`. Used for
  screenshot/recording serial numbers.

## Internal layout

```
engine/utility/
└── ir_utility.hpp            — umbrella
    └── utility/
        ├── file_utils.hpp
        └── path_utils.hpp
```

## Gotchas

- **`readFileAsString` throws.** Catch it where you care; don't let the
  exception cross a callback boundary.
- **Separator handling is C++-string-based.** It assumes forward slashes
  work on Windows (they do, mostly). If a path eventually reaches a Win32
  API that insists on backslashes, normalize at the call site.
- **No directory creation.** `joinPath` does not `mkdir -p`. The caller
  is responsible for making sure the target directory exists before
  writing.
- **Don't grow this into a kitchen sink.** If you're adding a string
  helper, a time helper, or a random helper, put it in the module that
  needs it (`math/` for random, `profile/` for time scaffolding, etc.)
  — `utility/` is intentionally narrow.
