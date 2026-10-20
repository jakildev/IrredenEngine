-- Fixed four-zone layout in GUI-canvas trixels (640x720 at the 1280x720 /
-- gui_scale = 1 config; a trixel is two screen pixels wide and one tall).
-- main_lua.cpp's GUI-session shot tables click at screen positions derived
-- from these numbers, so a change here moves those clicks too.

local L = {}

L.header = { x = 0, y = 0, w = 640, h = 28 }
L.header.settings = { x = 568, y = 5, w = 66, h = 18 }
L.header.update = { x = 490, y = 5, w = 72, h = 18 }
L.header.banner = { x = 150, y = 10 }
L.header.openLog = { x = 400, y = 5, w = 84, h = 18 }

L.tree = { x = 0, y = 30, w = 200, h = 450 }
L.tree.filter = { x = 6, y = 48, w = 188, h = 16 }
L.tree.rowTop = 72
L.tree.rowStep = 12
L.tree.rows = 31
L.tree.indent = 6
L.tree.up = { x = 6, y = 458, w = 30, h = 16 }
L.tree.down = { x = 40, y = 458, w = 30, h = 16 }
L.tree.count = { x = 78, y = 462 }

L.details = { x = 202, y = 30, w = 300, h = 450 }
L.details.left = 210
L.details.cols = 46
L.details.name = { x = 210, y = 48 }
L.details.fav = { x = 440, y = 46, w = 56, h = 16 }
L.details.descriptionTop = 66
L.details.lineStep = 10
L.details.descriptionLines = 3
L.details.tagsY = 98
L.details.dirY = 110
L.details.ide = { x = 210, y = 122, w = 40, h = 16 }
L.details.folder = { x = 254, y = 122, w = 52, h = 16 }
L.details.docsTop = 142
L.details.docStep = 18
L.details.docs = 2
L.details.targetTop = 182
L.details.targetStep = 48
L.details.targets = 5
L.details.preset = { x = 372, w = 124, h = 14 }
L.details.build = { x = 372, dy = 18, w = 40, h = 16 }
L.details.run = { x = 416, dy = 18, w = 40, h = 16 }
L.details.more = { x = 460, y = 460, w = 36, h = 16 }

L.settings = {}
L.settings.title = { x = 210, y = 48 }
L.settings.editorLabel = { x = 210, y = 70 }
L.settings.editor = { x = 210, y = 82, w = 200, h = 14 }
L.settings.favourites = { x = 210, y = 140 }
L.settings.save = { x = 210, y = 160, w = 48, h = 16 }
L.settings.revert = { x = 262, y = 160, w = 60, h = 16 }
L.settings.close = { x = 326, y = 160, w = 54, h = 16 }

L.assets = { x = 504, y = 30, w = 136, h = 450 }

L.log = { x = 0, y = 482, w = 640, h = 238 }
L.log.result = { x = 8, y = 500 }
L.log.lineTop = 514
L.log.lineStep = 10
L.log.rows = 18
L.log.cols = 104
L.log.up = { x = 6, y = 698, w = 30, h = 16 }
L.log.down = { x = 40, y = 698, w = 30, h = 16 }
L.log.tail = { x = 74, y = 698, w = 40, h = 16 }
L.log.open = { x = 548, y = 698, w = 84, h = 16 }

return L
