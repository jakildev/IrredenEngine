#pragma once

namespace IRScript {

    enum LuaType {
        NIL,
        BOOLEAN,
        NUMBER,
        STRING,
        USERDATA,
        FUNCTION,
        THREAD,
        TABLE
    };

} // namespace IRScript