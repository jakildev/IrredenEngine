# engine/profile/ — logging and CPU profiling

Thin wrappers around spdlog (logging) and easy_profiler (instrumentation).
Exposes one header full of macros that compile to no-ops in release
builds.

## Entry point

`engine/profile/ir_profile.hpp` — macros and free-function wrappers. No
class to instantiate on the caller side; the underlying `LoggerSpd` and
`CPUProfiler` are singletons.

## Logging macros

Three independent sinks, one per logger:

- `IR_LOG_<LEVEL>(fmt, ...)` — **game/client** logger. Use in creation
  code.
- `IRE_LOG_<LEVEL>(fmt, ...)` — **engine** logger. Use in `engine/**`
  code.
- `IRE_GL_LOG_<LEVEL>(fmt, ...)` — **GL debug** logger. Driven by GL
  debug callbacks.

Levels: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`.

Under the hood each macro calls `fmt::runtime(fmt)` so dynamic format
strings work. In `IR_RELEASE` mode every log macro is an empty statement
— don't rely on side effects in arguments.

## Assertion

```cpp
IR_ASSERT(cond, "formatted message %d", value);
```

On failure: logs a critical GL error via the engine logger and throws
`std::runtime_error`. In `IR_RELEASE` it's still evaluated (the
assertion is not eliminated) but no log is emitted.

## Profiling macros

Wrap easy_profiler:

- `IR_PROFILE_FUNCTION(color)` — name taken from `__FUNCTION__`.
- `IR_PROFILE_BLOCK(name, color)` — named scope.
- `IR_PROFILE_END_BLOCK` — manual block close.

Colors are ABGR hex: `0xff0000ff` = pure blue, `0xff00ff00` = green, etc.

`CPUProfiler::setEnabled(bool)` gates all profile macros. Call it from a
creation's startup if you want profiling off by default.

## Internal layout

```
engine/profile/
├── ir_profile.hpp             — public macros
├── ir_profile.tpp             — template impls for log macros
└── profile/
    ├── logger_spd.hpp         — spdlog singleton (engine/gl/game sinks)
    └── cpu_profiler.hpp       — easy_profiler singleton
```

## Gotchas

- **Macros are no-ops in release.** Don't put side-effectful calls inside
  log macro args.
- **Dynamic format strings use `fmt::runtime`.** Passing a raw
  user-supplied `std::string` as the format string is a footgun — if it
  contains `{}`, fmt will treat them as format specifiers. Wrap user
  input explicitly.
- **Wrong logger macro = routed to the wrong sink.** Don't use
  `IR_LOG_*` from engine code; it'll land in the game log.
- **`IR_ASSERT` throws.** If you're in a `noexcept` scope, `IR_ASSERT`
  calls `std::terminate`. Prefer early-return plus an `IRE_LOG_ERROR` in
  those spots.
- **easy_profiler has overhead.** Leaving profile blocks enabled in a
  production build costs real cycles. Either gate them with `IR_RELEASE`
  or call `setEnabled(false)` at startup.
