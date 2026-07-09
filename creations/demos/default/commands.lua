-- T-193 PR 2: Lua equivalent of the pre-T-193 C++ initCommands() block.
-- Same 18 bindings the C++ version had, in the same order. Documents the
-- canonical IRCommand / IRInput surface for any creation porting off the
-- C++ initCommands() pattern.

local CN = IRCommand.CommandName
local IT = IRInput.InputType
local BS = IRInput.ButtonStatus
local K = IRInput.Key
local CTRL = IRInput.Modifier.CONTROL

IRCommand.bindPrefab(CN.CLOSE_WINDOW,           IT.KEY_MOUSE, BS.PRESSED,  K.ESCAPE)
IRCommand.bindPrefab(CN.ZOOM_IN,                IT.KEY_MOUSE, BS.PRESSED,  K.EQUAL)
IRCommand.bindPrefab(CN.ZOOM_OUT,               IT.KEY_MOUSE, BS.PRESSED,  K.MINUS)
IRCommand.bindPrefab(CN.MOVE_CAMERA_DOWN_START, IT.KEY_MOUSE, BS.PRESSED,  K.S)
IRCommand.bindPrefab(CN.MOVE_CAMERA_UP_START,   IT.KEY_MOUSE, BS.PRESSED,  K.W)
IRCommand.bindPrefab(CN.MOVE_CAMERA_RIGHT_START,IT.KEY_MOUSE, BS.PRESSED,  K.D)
IRCommand.bindPrefab(CN.MOVE_CAMERA_LEFT_START, IT.KEY_MOUSE, BS.PRESSED,  K.A)
IRCommand.bindPrefab(CN.MOVE_CAMERA_DOWN_END,   IT.KEY_MOUSE, BS.RELEASED, K.S)
IRCommand.bindPrefab(CN.MOVE_CAMERA_UP_END,     IT.KEY_MOUSE, BS.RELEASED, K.W)
IRCommand.bindPrefab(CN.MOVE_CAMERA_RIGHT_END,  IT.KEY_MOUSE, BS.RELEASED, K.D)
IRCommand.bindPrefab(CN.MOVE_CAMERA_LEFT_END,   IT.KEY_MOUSE, BS.RELEASED, K.A)
IRCommand.bindPrefab(CN.SCREENSHOT,             IT.KEY_MOUSE, BS.PRESSED,  K.F8)
IRCommand.bindPrefab(CN.SCREENSHOT_CANVAS,      IT.KEY_MOUSE, BS.PRESSED,  K.F7)
IRCommand.bindPrefab(CN.RECORD_TOGGLE,          IT.KEY_MOUSE, BS.PRESSED,  K.F9)
IRCommand.bindPrefab(CN.TOGGLE_GUI,             IT.KEY_MOUSE, BS.PRESSED,  K.GRAVE)
IRCommand.bindPrefab(CN.GUI_ZOOM_IN,            IT.KEY_MOUSE, BS.PRESSED,  K.EQUAL, CTRL)
IRCommand.bindPrefab(CN.GUI_ZOOM_OUT,           IT.KEY_MOUSE, BS.PRESSED,  K.MINUS, CTRL)
IRCommand.bindPrefab(CN.TOGGLE_CULLING_FREEZE,  IT.KEY_MOUSE, BS.PRESSED,  K.F10)
IRCommand.bindPrefab(CN.TOGGLE_CULLING_MINIMAP, IT.KEY_MOUSE, BS.PRESSED,  K.F11)
