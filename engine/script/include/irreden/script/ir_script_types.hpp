#pragma once

namespace IRScript {

    enum LuaType {
        NIL,
        BOOLEAN,
        INTEGER, // DEFINED BY IRREDEN
        NUMBER,
        STRING,
        USERDATA,
        FUNCTION,
        THREAD,
        TABLE
    };

} // namespace IRScript