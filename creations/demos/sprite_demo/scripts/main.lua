-- sprite_demo: exercises C_Sprite / C_SpriteAnimation Lua bindings.
-- Demonstrates three loop modes and a mid-playback sub-animation switch.
--
-- Sheet layout: test_sprites.png (32x32, 2x2 grid, 16x16 cells)
--   frame 0 = red,  frame 1 = green,  frame 2 = blue,  frame 3 = yellow
--   anim "spin"        -- all 4 frames, 6fps  (used by LOOP demo sprite)
--   anim "hold_red"    -- frame 0 only         (used by ONCE demo sprite)
--   anim "hold_green"  -- frame 1 only         (used by PING_PONG demo sprite)

local ASSET_DIR = "assets/sprite_demo"

-- Load the single test sheet (one entity holds C_SpriteSheet).
local sheet = ir.sprite.loadSheet("test_sprites", ASSET_DIR)

-- Three sprites at different iso-Z depths so back-to-front sort is visible.
-- Sprite A: LOOP — spins through all 4 frames indefinitely.
local spriteA = ir.sprite.create(sheet, -4.0, 0.0, 2.0)
ir.sprite.playAnimation(spriteA, sheet, "spin", ir.sprite.LOOP, 1.0)

-- Sprite B: ONCE — holds on frame 0 (red), terminates after one play.
local spriteB = ir.sprite.create(sheet, 0.0, 0.0, 2.0)
ir.sprite.playAnimation(spriteB, sheet, "hold_red", ir.sprite.ONCE)

-- Sprite C: PING_PONG — uses 'spin' in PING_PONG mode so the back-and-forth
-- reversal across all 4 frames is apparent.
local spriteC = ir.sprite.create(sheet, 4.0, 0.0, 2.0)
ir.sprite.playAnimation(spriteC, sheet, "spin", ir.sprite.PING_PONG, 0.5)

-- Mid-playback sub-animation switch is exercised by --auto-screenshot running
-- enough frames to capture animated state. A per-frame Lua UPDATE system to
-- trigger it explicitly will be wired in when T-101 (Lua system registration)
-- ships.

-- Blend pair, set apart from sprites A, B and C so no animated frame sits
-- under it. Sprite D holds on frame 0 (red, fully opaque). Sprite E
-- draws from test_half_alpha.png, whose every texel is (255, 255, 255, 128),
-- and is offset so it covers part of D and part of the bare background at
-- both screenshot zooms. A blended SPRITE_TO_SCREEN draw shows D through E;
-- a replacing draw shows flat white. The quads are a fixed 16 px while the
-- offset between them grows with zoom and output scale: one world unit apart
-- they still overlap at zoom 2 on a 2x display, two apart they do not.
local spriteD = ir.sprite.create(sheet, 0.0, 16.0, 2.0)
ir.sprite.playAnimation(spriteD, sheet, "hold_red", ir.sprite.ONCE)

local halfAlphaSheet = ir.sprite.loadSheet("test_half_alpha", ASSET_DIR)
ir.sprite.create(halfAlphaSheet, 0.0, 15.0, 2.0)
