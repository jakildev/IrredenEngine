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
-- The worktree picker, shown only while two or more roots are configured.
L.header.picker = { x = 140, y = 7, w = 200, h = 14 }
L.header.pickerBesideBanner = { x = 290, y = 7, w = 104, h = 14 }

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
L.details.package = { x = 458, dy = 18, w = 42, h = 16 }
L.details.packageDy = 36
L.details.more = { x = 460, y = 460, w = 36, h = 16 }

L.settings = {}
L.settings.title = { x = 210, y = 48 }
L.settings.editorLabel = { x = 210, y = 70 }
L.settings.editor = { x = 210, y = 82, w = 200, h = 14 }
L.settings.favourites = { x = 210, y = 140 }
L.settings.save = { x = 210, y = 160, w = 48, h = 16 }
L.settings.revert = { x = 262, y = 160, w = 60, h = 16 }
L.settings.close = { x = 326, y = 160, w = 54, h = 16 }
L.settings.roots = { title = { x = 210, y = 182 }, x = 210, top = 196, step = 18, rows = 14,
                     buttonW = 44, buttonH = 16, labelDx = 50,
                     more = { x = 446, y = 178, w = 50, h = 16 } }

L.assets = { x = 504, y = 30, w = 136, h = 450 }
L.assets.left = 510
L.assets.tabY = 48
L.assets.tabWidth = 27
L.assets.tabStep = 29
L.assets.rowTop = 68
L.assets.rowStep = 16
L.assets.rows = 6
L.assets.listWidth = 120
L.assets.up = { x = 510, y = 164, w = 30, h = 16 }
L.assets.down = { x = 544, y = 164, w = 30, h = 16 }
L.assets.count = { x = 580, y = 168 }
L.assets.metadataTop = 184
L.assets.metadataStep = 10
L.assets.metadataRows = 4
L.assets.preview = { x = 510, y = 230, w = 120, h = 180 }
L.assets.ide = { x = 590, y = 414, w = 40, h = 16 }

L.details.thumbnail = { x = 412, y = 64, w = 82, h = 54 }

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

local function setRect(target, x, y, w, h)
    target.x, target.y, target.w, target.h = x, y, w, h
end

function L.update(tree, details, assets, log)
    setRect(L.tree, tree.x, tree.y, tree.w, tree.h)
    setRect(L.tree.filter, tree.x + 6, tree.y + 18, tree.w - 12, 16)
    L.tree.rowTop = tree.y + 42
    setRect(L.tree.up, tree.x + 6, tree.y + tree.h - 22, 30, 16)
    setRect(L.tree.down, tree.x + 40, tree.y + tree.h - 22, 30, 16)
    L.tree.count.x, L.tree.count.y = tree.x + 78, tree.y + tree.h - 18

    setRect(L.details, details.x, details.y, details.w, details.h)
    L.details.left = details.x + 8
    L.details.cols = math.max(16, math.floor((details.w - 24) / 6))
    L.details.name.x, L.details.name.y = details.x + 8, details.y + 18
    setRect(L.details.fav, details.x + details.w - 62, details.y + 16, 56, 16)
    L.details.descriptionTop = details.y + 36
    L.details.tagsY = details.y + 68
    L.details.dirY = details.y + 80
    setRect(L.details.ide, details.x + 8, details.y + 92, 40, 16)
    setRect(L.details.folder, details.x + 52, details.y + 92, 52, 16)
    L.details.docsTop = details.y + 112
    L.details.targetTop = details.y + 152
    L.details.preset.x = details.x + details.w - 130
    L.details.build.x = details.x + details.w - 130
    L.details.run.x = details.x + details.w - 86
    L.details.package.x = details.x + details.w - 44
    L.details.more.x, L.details.more.y = details.x + details.w - 42, details.y + details.h - 20
    setRect(L.details.thumbnail, details.x + details.w - 90, details.y + 34,
        math.min(82, details.w - 16), 54)

    L.settings.title.x, L.settings.title.y = details.x + 8, details.y + 18
    L.settings.editorLabel.x, L.settings.editorLabel.y = details.x + 8, details.y + 40
    setRect(L.settings.editor, details.x + 8, details.y + 52,
        math.max(80, details.w - 16), 14)
    L.settings.favourites.x, L.settings.favourites.y = details.x + 8, details.y + 110
    setRect(L.settings.save, details.x + 8, details.y + 130, 48, 16)
    setRect(L.settings.revert, details.x + 60, details.y + 130, 60, 16)
    setRect(L.settings.close, details.x + 124, details.y + 130, 54, 16)
    L.settings.roots.title.x, L.settings.roots.title.y = details.x + 8, details.y + 152
    L.settings.roots.x, L.settings.roots.top = details.x + 8, details.y + 166
    L.settings.roots.rows = math.max(1, math.floor((details.h - 174) / L.settings.roots.step))
    setRect(L.settings.roots.more, details.x + details.w - 56, details.y + 148, 50, 16)

    setRect(L.assets, assets.x, assets.y, assets.w, assets.h)
    L.assets.left = assets.x + 6
    L.assets.tabY = assets.y + 18
    L.assets.rowTop = assets.y + 38
    setRect(L.assets.up, assets.x + 6, assets.y + 134, 30, 16)
    setRect(L.assets.down, assets.x + 40, assets.y + 134, 30, 16)
    L.assets.count.x, L.assets.count.y = assets.x + 76, assets.y + 138
    L.assets.metadataTop = assets.y + 154
    setRect(L.assets.preview, assets.x + 6, assets.y + 200,
        math.max(40, assets.w - 12), math.max(40, assets.h - 270))
    setRect(L.assets.ide, assets.x + assets.w - 50, assets.y + assets.h - 36, 40, 16)
    L.assets.listWidth = math.max(40, assets.w - 16)

    setRect(L.log, log.x, log.y, log.w, log.h)
    L.log.result.x, L.log.result.y = log.x + 8, log.y + 18
    L.log.lineTop = log.y + 32
    L.log.rows = math.max(1, math.floor((log.h - 58) / L.log.lineStep))
    L.log.cols = math.max(16, math.floor((log.w - 16) / 6))
    setRect(L.log.up, log.x + 6, log.y + log.h - 22, 30, 16)
    setRect(L.log.down, log.x + 40, log.y + log.h - 22, 30, 16)
    setRect(L.log.tail, log.x + 74, log.y + log.h - 22, 40, 16)
    setRect(L.log.open, log.x + log.w - 92, log.y + log.h - 22, 84, 16)
end

return L
