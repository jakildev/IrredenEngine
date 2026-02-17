#pragma once

namespace IRScript {
class LuaScript;

template <typename T> inline constexpr bool kHasLuaBinding = false;

template <typename T> void bindLuaType(LuaScript &);
} // namespace IRScript
