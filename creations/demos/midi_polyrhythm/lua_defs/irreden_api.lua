---@meta
-- LuaCATS definitions for the Irreden Engine Lua API.
-- This file is read by the Lua Language Server for autocompletion and type
-- checking.  It is NOT executed at runtime.

-- ═══════════════════════════════════════════════════════════════════════════
-- Rhythm presets
-- ═══════════════════════════════════════════════════════════════════════════

---@enum RhythmPreset
RhythmPreset = {
    heartbeat     = "heartbeat",
    prime_pair    = "prime_pair",
    triple        = "triple",
    clockwork     = "clockwork",
    pentad        = "pentad",
    mesmerize     = "mesmerize",
    reich_phase   = "reich_phase",
    cascade       = "cascade",
    nine_smooth   = "nine_smooth",
    dense_twelve  = "dense_twelve",
    full_360      = "full_360",
    slow_evolve   = "slow_evolve",
    wave_1m_fast  = "wave_1m_fast",
    wave_1m_slow  = "wave_1m_slow",
    wave_2m_fast  = "wave_2m_fast",
    wave_2m_slow  = "wave_2m_slow",
    wave_3m_slow  = "wave_3m_slow",
}

-- ═══════════════════════════════════════════════════════════════════════════
-- Primitive vector / color types
-- ═══════════════════════════════════════════════════════════════════════════

---@class vec2
---@field x number
---@field y number
---@operator add(vec2): vec2
---@operator sub(vec2): vec2
---@operator mul(number): vec2
local vec2 = {}
---@param x number
---@param y number
---@return vec2
function vec2.new(x, y) end

---@class vec3
---@field x number
---@field y number
---@field z number
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(number): vec3
local vec3 = {}
---@param x number
---@param y number
---@param z number
---@return vec3
function vec3.new(x, y, z) end

---@class ivec2
---@field x integer
---@field y integer
local ivec2 = {}
---@param x integer
---@param y integer
---@return ivec2
function ivec2.new(x, y) end

---@class ivec3
---@field x integer
---@field y integer
---@field z integer
local ivec3 = {}
---@param x integer
---@param y integer
---@param z integer
---@return ivec3
function ivec3.new(x, y, z) end

---@class Color
---@field r integer
---@field g integer
---@field b integer
---@field a integer
local Color = {}
---@param r integer
---@param g integer
---@param b integer
---@param a integer
---@return Color
function Color.new(r, g, b, a) end

---@class ColorHSV
---@field h number
---@field s number
---@field v number
---@field a number
local ColorHSV = {}
---@param h number
---@param s number
---@param v number
---@param a number
---@return ColorHSV
---@overload fun(): ColorHSV
function ColorHSV.new(h, s, v, a) end

-- ═══════════════════════════════════════════════════════════════════════════
-- Enums
-- ═══════════════════════════════════════════════════════════════════════════

---@enum IREasingFunction
IREasingFunction = {
    LINEAR_INTERPOLATION    = 0,
    QUADRATIC_EASE_IN       = 1,
    QUADRATIC_EASE_OUT      = 2,
    QUADRATIC_EASE_IN_OUT   = 3,
    CUBIC_EASE_IN           = 4,
    CUBIC_EASE_OUT          = 5,
    CUBIC_EASE_IN_OUT       = 6,
    QUARTIC_EASE_IN         = 7,
    QUARTIC_EASE_OUT        = 8,
    QUARTIC_EASE_IN_OUT     = 9,
    QUINTIC_EASE_IN         = 10,
    QUINTIC_EASE_OUT        = 11,
    QUINTIC_EASE_IN_OUT     = 12,
    SINE_EASE_IN            = 13,
    SINE_EASE_OUT           = 14,
    SINE_EASE_IN_OUT        = 15,
    CIRCULAR_EASE_IN        = 16,
    CIRCULAR_EASE_OUT       = 17,
    CIRCULAR_EASE_IN_OUT    = 18,
    EXPONENTIAL_EASE_IN     = 19,
    EXPONENTIAL_EASE_OUT    = 20,
    EXPONENTIAL_EASE_IN_OUT = 21,
    ELASTIC_EASE_IN         = 22,
    ELASTIC_EASE_OUT        = 23,
    ELASTIC_EASE_IN_OUT     = 24,
    BACK_EASE_IN            = 25,
    BACK_EASE_OUT           = 26,
    BACK_EASE_IN_OUT        = 27,
    BOUNCE_EASE_IN          = 28,
    BOUNCE_EASE_OUT         = 29,
    BOUNCE_EASE_IN_OUT      = 30,
}

---@enum MidiNote
MidiNote = {
    A0  = 21, Bb0 = 22, B0  = 23,
    C1  = 24, Db1 = 25, D1  = 26, Eb1 = 27, E1  = 28, F1  = 29,
    Gb1 = 30, G1  = 31, Ab1 = 32, A1  = 33, Bb1 = 34, B1  = 35,
    C2  = 36, Db2 = 37, D2  = 38, Eb2 = 39, E2  = 40, F2  = 41,
    Gb2 = 42, G2  = 43, Ab2 = 44, A2  = 45, Bb2 = 46, B2  = 47,
    C3  = 48, Db3 = 49, D3  = 50, Eb3 = 51, E3  = 52, F3  = 53,
    Gb3 = 54, G3  = 55, Ab3 = 56, A3  = 57, Bb3 = 58, B3  = 59,
    C4  = 60, Db4 = 61, D4  = 62, Eb4 = 63, E4  = 64, F4  = 65,
    Gb4 = 66, G4  = 67, Ab4 = 68, A4  = 69, Bb4 = 70, B4  = 71,
    C5  = 72, Db5 = 73, D5  = 74, Eb5 = 75, E5  = 76, F5  = 77,
    Gb5 = 78, G5  = 79, Ab5 = 80, A5  = 81, Bb5 = 82, B5  = 83,
    C6  = 84, D6  = 86, E6  = 88, F6  = 89, G6  = 91, A6  = 93,
    B6  = 95, C7  = 96, C8  = 108,
}

---@enum NoteName
NoteName = {
    C  = 0,  Cs = 1,  Db = 1,
    D  = 2,  Ds = 3,  Eb = 3,
    E  = 4,  F  = 5,  Fs = 6,  Gb = 6,
    G  = 7,  Gs = 8,  Ab = 8,
    A  = 9,  As = 10, Bb = 10,
    B  = 11,
}

---@enum ScaleMode
ScaleMode = {
    -- Diatonic (Church modes)
    IONIAN             = 0,
    MAJOR              = 0,
    DORIAN             = 1,
    PHRYGIAN           = 2,
    LYDIAN             = 3,
    MIXOLYDIAN         = 4,
    AEOLIAN            = 5,
    MINOR              = 5,
    LOCRIAN            = 6,
    -- Harmonic / melodic variants
    HARMONIC_MINOR     = 7,
    MELODIC_MINOR      = 8,
    HUNGARIAN_MINOR    = 9,
    DOUBLE_HARMONIC    = 10,
    NEAPOLITAN_MINOR   = 11,
    NEAPOLITAN_MAJOR   = 12,
    ENIGMATIC          = 13,
    PERSIAN            = 14,
    -- Pentatonic
    PENTATONIC_MAJOR   = 15,
    PENTATONIC_MINOR   = 16,
    HIRAJOSHI          = 17,
    IN_SEN             = 18,
    IWATO              = 19,
    PELOG              = 20,
    -- Hexatonic
    WHOLE_TONE         = 21,
    BLUES              = 22,
    AUGMENTED          = 23,
    PROMETHEUS         = 24,
    TRITONE            = 25,
    -- Octatonic
    DIMINISHED_WHOLE_HALF = 26,
    DIMINISHED_HALF_WHOLE = 27,
    BEBOP_DOMINANT     = 28,
    BEBOP_MAJOR        = 29,
    -- Chromatic
    CHROMATIC          = 30,
}

---@enum BackgroundTypes
BackgroundTypes = {
    SINGLE_COLOR    = 0,
    GRADIENT        = 1,
    GRADIENT_RANDOM = 2,
    PULSE_PATTERN   = 3,
}

---@enum IRCollisionLayer
IRCollisionLayer = {
    NOTE_BLOCK    = 0,
    NOTE_PLATFORM = 0,
    PARTICLE      = 0,
    DEFAULT       = 0,
}

-- ═══════════════════════════════════════════════════════════════════════════
-- Entity wrapper
-- ═══════════════════════════════════════════════════════════════════════════

---@class LuaEntity
---@field entity integer
local LuaEntity = {}
---@param id integer
---@return LuaEntity
function LuaEntity.new(id) end

---@class CreateEntityCallbackParams
---@field center ivec3
---@field index vec3
local CreateEntityCallbackParams = {}
---@param center ivec3
---@param index vec3
---@return CreateEntityCallbackParams
function CreateEntityCallbackParams.new(center, index) end

-- ═══════════════════════════════════════════════════════════════════════════
-- Component types
-- ═══════════════════════════════════════════════════════════════════════════

---@class C_Position3D
---@field x number
---@field y number
---@field z number
local C_Position3D = {}
---@param x number
---@param y number
---@param z number
---@return C_Position3D
---@overload fun(v: vec3): C_Position3D
function C_Position3D.new(x, y, z) end

---@class C_Velocity3D
local C_Velocity3D = {}
---@param x number
---@param y number
---@param z number
---@return C_Velocity3D
---@overload fun(v: vec3): C_Velocity3D
---@overload fun(): C_Velocity3D
function C_Velocity3D.new(x, y, z) end

---@class C_VoxelSetNew
local C_VoxelSetNew = {}
---@param size ivec3
---@param color Color
---@return C_VoxelSetNew
function C_VoxelSetNew.new(size, color) end

---@class PeriodStage
local PeriodStage = {}
---@param startValue number
---@param endValue number
---@param durationSeconds number
---@param phaseOffset number
---@param easingFunction IREasingFunction
---@param isAbsolute boolean
---@return PeriodStage
function PeriodStage.new(startValue, endValue, durationSeconds, phaseOffset, easingFunction, isAbsolute) end

---@class C_PeriodicIdle
local C_PeriodicIdle = {}
---@param x number
---@param y number
---@param z number
---@return C_PeriodicIdle
---@overload fun(offset: vec3, period: number, phase: number): C_PeriodicIdle
function C_PeriodicIdle.new(x, y, z) end
---@param stage PeriodStage
function C_PeriodicIdle:addStageDurationSeconds(stage) end
function C_PeriodicIdle:requestPauseAtCycleStart() end
function C_PeriodicIdle:resume() end
---@param delaySeconds number
function C_PeriodicIdle:resumeWithDelay(delaySeconds) end
---@return boolean
function C_PeriodicIdle:isPaused() end
---@return boolean
function C_PeriodicIdle:isPauseRequested() end

---@class C_MidiNote
---@field note integer
---@field velocity integer
---@field channel integer
---@field holdSeconds number
local C_MidiNote = {}
---@param note integer
---@param velocity integer
---@param channel integer
---@param holdSeconds number
---@return C_MidiNote
---@overload fun(note: integer, velocity: integer): C_MidiNote
function C_MidiNote.new(note, velocity, channel, holdSeconds) end

---@class C_MidiSequence
---@field bpm number
---@field looping boolean
local C_MidiSequence = {}
---@param bpm number
---@param beatsPerMeasure integer
---@param numMeasures integer
---@param channel integer
---@param looping? boolean
---@return C_MidiSequence
function C_MidiSequence.new(bpm, beatsPerMeasure, numMeasures, channel, looping) end
---@param note integer
---@param velocity integer
---@param startBeat number
---@param durationBeats number
function C_MidiSequence:insertNote(note, velocity, startBeat, durationBeats) end
---@return number
function C_MidiSequence:getMeasureLengthSeconds() end
---@return number
function C_MidiSequence:getSequenceLengthSeconds() end

---@class C_ParticleBurst
---@field count integer
---@field lifetime integer
---@field speed number
---@field upwardAcceleration number
---@field dragScaleX number
---@field dragScaleY number
---@field dragScaleZ number
---@field spawnOffsetZ number
---@field isoDepthOffset number
---@field xySpeedRatio number
---@field zSpeedRatio number
---@field zVarianceRatio number
---@field pDragPerSecond number
---@field pDriftDelaySeconds number
---@field pDriftUpAccelPerSec number
---@field pDragMinSpeed number
---@field pHoverDurationSec number
---@field pHoverOscSpeed number
---@field pHoverOscAmplitude number
---@field pHoverBlendSec number
---@field pHoverBlendEasing IREasingFunction
---@field hoverStartVariance number
---@field hoverDurationVariance number
---@field hoverAmplitudeVariance number
---@field hoverSpeedVariance number
---@field glowEnabled boolean
---@field glowColor Color
---@field glowHoldSeconds number
---@field glowFadeSeconds number
---@field glowEasing IREasingFunction
local C_ParticleBurst = {}
---@param count integer
---@param lifetime integer
---@param speed number
---@param upwardAcceleration? number
---@param dragScaleX? number
---@param dragScaleY? number
---@param dragScaleZ? number
---@param spawnOffsetZ? number
---@param isoDepthOffset? number
---@return C_ParticleBurst
---@overload fun(): C_ParticleBurst
function C_ParticleBurst.new(count, lifetime, speed, upwardAcceleration, dragScaleX, dragScaleY, dragScaleZ, spawnOffsetZ, isoDepthOffset) end

---@class C_ColliderIso3DAABB
---@field halfExtents vec3
---@field centerOffset vec3
local C_ColliderIso3DAABB = {}
---@param halfExtents vec3
---@param centerOffset vec3
---@return C_ColliderIso3DAABB
---@overload fun(hx: number, hy: number, hz: number): C_ColliderIso3DAABB
---@overload fun(): C_ColliderIso3DAABB
function C_ColliderIso3DAABB.new(halfExtents, centerOffset) end

---@class C_CollisionLayer
---@field layer integer
---@field collidesWithMask integer
---@field isSolid boolean
local C_CollisionLayer = {}
---@param layer integer
---@param collidesWithMask integer
---@param isSolid boolean
---@return C_CollisionLayer
---@overload fun(): C_CollisionLayer
function C_CollisionLayer.new(layer, collidesWithMask, isSolid) end

---@class C_ContactEvent
local C_ContactEvent = {}
---@return C_ContactEvent
function C_ContactEvent.new() end

---@class C_ReactiveReturn3D
---@field triggerImpulseVelocity vec3
---@field springStrength number
---@field dampingPerSecond number
---@field maxRebounds integer
---@field settleDistance number
---@field settleSpeed number
---@field triggerOnContactEnter boolean
local C_ReactiveReturn3D = {}
---@param triggerImpulseVelocity vec3
---@param springStrength number
---@param dampingPerSecond number
---@param maxRebounds integer
---@param settleDistance number
---@param settleSpeed number
---@param triggerOnContactEnter boolean
---@return C_ReactiveReturn3D
---@overload fun(): C_ReactiveReturn3D
function C_ReactiveReturn3D.new(triggerImpulseVelocity, springStrength, dampingPerSecond, maxRebounds, settleDistance, settleSpeed, triggerOnContactEnter) end

---@class C_RhythmicLaunch
---@field periodSeconds number
---@field impulseVelocity vec3
---@field restOffsetZ number
---@field elapsedSeconds number
---@field grounded boolean
local C_RhythmicLaunch = {}
---@param periodSeconds number
---@param impulseVelocity vec3
---@param restOffsetZ? number
---@param elapsedSeconds? number
---@param grounded? boolean
---@return C_RhythmicLaunch
---@overload fun(): C_RhythmicLaunch
function C_RhythmicLaunch.new(periodSeconds, impulseVelocity, restOffsetZ, elapsedSeconds, grounded) end

---@class C_TriggerGlow
---@field targetColor Color
---@field holdSeconds number
---@field fadeSeconds number
---@field easingFunction IREasingFunction
---@field triggerOnContactEnter boolean
local C_TriggerGlow = {}
---@param targetColor Color
---@param holdSeconds number
---@param fadeSeconds number
---@param easingFunction IREasingFunction
---@param triggerOnContactEnter boolean
---@return C_TriggerGlow
---@overload fun(): C_TriggerGlow
function C_TriggerGlow.new(targetColor, holdSeconds, fadeSeconds, easingFunction, triggerOnContactEnter) end

---@class C_TriangleCanvasBackground
local C_TriangleCanvasBackground = {}
---@param bgType BackgroundTypes
---@param colorA Color
---@param colorB Color
---@param size ivec2
---@param pulseSpeed number
---@param patternScale integer
---@return C_TriangleCanvasBackground
---@overload fun(): C_TriangleCanvasBackground
function C_TriangleCanvasBackground.new(bgType, colorA, colorB, size, pulseSpeed, patternScale) end
function C_TriangleCanvasBackground:zoomPatternIn() end
function C_TriangleCanvasBackground:zoomPatternOut() end
---@param multiplier number
function C_TriangleCanvasBackground:setPatternZoomMultiplier(multiplier) end
---@param dir vec2
function C_TriangleCanvasBackground:setPulseWaveDirection(dir) end
---@param startDir vec2
---@param endDir vec2
---@param periodSec number
---@param easeForward IREasingFunction
---@param easeBackward IREasingFunction
function C_TriangleCanvasBackground:setPulseWaveDirectionLinearMotion(startDir, endDir, periodSec, easeForward, easeBackward) end
function C_TriangleCanvasBackground:clearPulseWaveDirectionLinearMotion() end
---@param phaseScale number
---@param speedMultiplier number
---@param startOffset number
function C_TriangleCanvasBackground:setPulseWavePrimaryTiming(phaseScale, speedMultiplier, startOffset) end
---@param secondaryDir vec2
---@param secondaryPhaseScale number
---@param secondarySpeedMultiplier number
---@param secondaryStartOffset number
---@param mixAmount number
function C_TriangleCanvasBackground:setPulseWaveInterference(secondaryDir, secondaryPhaseScale, secondarySpeedMultiplier, secondaryStartOffset, mixAmount) end
---@param startDir vec2
---@param endDir vec2
---@param periodSec number
---@param easeForward IREasingFunction
---@param easeBackward IREasingFunction
function C_TriangleCanvasBackground:setPulseWaveSecondaryDirectionLinearMotion(startDir, endDir, periodSec, easeForward, easeBackward) end
function C_TriangleCanvasBackground:clearPulseWaveSecondaryDirectionLinearMotion() end
---@param phaseScale number
---@param speedMultiplier number
---@param startOffset number
function C_TriangleCanvasBackground:setPulseWaveSecondaryTiming(phaseScale, speedMultiplier, startOffset) end

---@class C_TrixelCanvasRenderBehavior
---@field useCameraPositionIso boolean
---@field useCameraZoom boolean
---@field applyRenderSubdivisions boolean
---@field mouseHoverEnabled boolean
---@field usePixelPerfectCameraOffset boolean
---@field parityOffsetIsoX number
---@field parityOffsetIsoY number
---@field staticPixelOffsetX number
---@field staticPixelOffsetY number
local C_TrixelCanvasRenderBehavior = {}
---@param useCameraPositionIso boolean
---@param useCameraZoom boolean
---@param applyRenderSubdivisions boolean
---@param mouseHoverEnabled boolean
---@param usePixelPerfectCameraOffset boolean
---@param parityOffsetIsoX number
---@param parityOffsetIsoY number
---@param staticPixelOffsetX number
---@param staticPixelOffsetY number
---@return C_TrixelCanvasRenderBehavior
---@overload fun(): C_TrixelCanvasRenderBehavior
function C_TrixelCanvasRenderBehavior.new(useCameraPositionIso, useCameraZoom, applyRenderSubdivisions, mouseHoverEnabled, usePixelPerfectCameraOffset, parityOffsetIsoX, parityOffsetIsoY, staticPixelOffsetX, staticPixelOffsetY) end

---@class C_ZoomLevel
---@field zoom number
local C_ZoomLevel = {}
---@param zoom? number
---@return C_ZoomLevel
function C_ZoomLevel.new(zoom) end
function C_ZoomLevel:zoomIn() end
function C_ZoomLevel:zoomOut() end

---@class C_HasGravity
local C_HasGravity = {}
---@return C_HasGravity
function C_HasGravity.new() end

---@enum AnimTriggerMode
AnimTriggerMode = {
    CONTACT_ENTER = 0,
    TIMER_SYNC    = 1,
    KEYPRESS      = 2,
    MANUAL        = 3,
}

---@class ActionAnimationPhase
---@field durationSeconds number
---@field startDisplacement number
---@field endDisplacement number
---@field easingFunction IREasingFunction
local ActionAnimationPhase = {}
---@param durationSeconds number
---@param startDisplacement number
---@param endDisplacement number
---@param easingFunction IREasingFunction
---@return ActionAnimationPhase
---@overload fun(): ActionAnimationPhase
function ActionAnimationPhase.new(durationSeconds, startDisplacement, endDisplacement, easingFunction) end

---@class C_AnimationClip
---@field phaseCount integer
---@field actionPhaseIndex integer
local C_AnimationClip = {}
---@return C_AnimationClip
function C_AnimationClip.new() end
---@param phase ActionAnimationPhase
function C_AnimationClip:addPhase(phase) end

---@class AnimationBinding
---@field trigger AnimTriggerMode
---@field clipEntity LuaEntity
---@field timerSyncLeadSeconds number
---@field canInterrupt boolean
local AnimationBinding = {}
---@param trigger AnimTriggerMode
---@param clipEntity LuaEntity
---@param timerSyncLeadSeconds number
---@param canInterrupt boolean
---@return AnimationBinding
---@overload fun(): AnimationBinding
function AnimationBinding.new(trigger, clipEntity, timerSyncLeadSeconds, canInterrupt) end

---@class C_ActionAnimation
---@field direction vec3
---@field currentDisplacement number
---@field bindingCount integer
---@field actionFired boolean
local C_ActionAnimation = {}
---@return C_ActionAnimation
function C_ActionAnimation.new() end
---@param binding AnimationBinding
---@return integer bindingIndex
function C_ActionAnimation:addBinding(binding) end

-- ── Animation color modifiers ──────────────────────────────────────────────

---@enum AnimColorBlendMode
AnimColorBlendMode = {
    REPLACE  = 0,
    MULTIPLY = 1,
    LERP     = 2,
}

---@enum AnimColorTrackMode
AnimColorTrackMode = {
    ABSOLUTE   = 0,
    HSV_OFFSET = 1,
    HSV_OFFSET_STATE_BLEND = 2,
    HSV_OFFSET_TIMELINE = 3,
}

---@class AnimPhaseColor
---@field startColor Color
---@field endColor Color
---@field easingFunction IREasingFunction
local AnimPhaseColor = {}
---@param startColor Color
---@param endColor Color
---@param easingFunction IREasingFunction
---@return AnimPhaseColor
---@overload fun(): AnimPhaseColor
function AnimPhaseColor.new(startColor, endColor, easingFunction) end

--- HSV offset per phase. Values are deltas applied to the entity's base color.
--- Hue wraps, saturation and value are clamped to [0,1].
---@class AnimPhaseColorMod
---@field startMod ColorHSV
---@field endMod ColorHSV
---@field easingFunction IREasingFunction
local AnimPhaseColorMod = {}
---@param startMod ColorHSV
---@param endMod ColorHSV
---@param easingFunction IREasingFunction
---@return AnimPhaseColorMod
---@overload fun(): AnimPhaseColorMod
function AnimPhaseColorMod.new(startMod, endMod, easingFunction) end

---@class AnimColorModTimelineSegment
---@field fromPhase integer
---@field toPhase integer
---@field startMod ColorHSV
---@field endMod ColorHSV
---@field easingFunction IREasingFunction
local AnimColorModTimelineSegment = {}
---@param fromPhase integer
---@param toPhase integer
---@param startMod ColorHSV
---@param endMod ColorHSV
---@param easingFunction IREasingFunction
---@return AnimColorModTimelineSegment
---@overload fun(): AnimColorModTimelineSegment
function AnimColorModTimelineSegment.new(fromPhase, toPhase, startMod, endMod, easingFunction) end

---@class C_AnimClipColorTrack
---@field mode AnimColorTrackMode
---@field phaseCount integer
---@field idleColor Color
---@field idleMod ColorHSV
---@field startMod ColorHSV
---@field endMod ColorHSV
local C_AnimClipColorTrack = {}
---@return C_AnimClipColorTrack
function C_AnimClipColorTrack.new() end
---@param phaseColor AnimPhaseColor
function C_AnimClipColorTrack:addPhaseColor(phaseColor) end
---@param phaseMod AnimPhaseColorMod
function C_AnimClipColorTrack:addPhaseMod(phaseMod) end
---@param segment AnimColorModTimelineSegment
function C_AnimClipColorTrack:addTimelineMod(segment) end

---@class C_AnimColorState
---@field blendMode AnimColorBlendMode
local C_AnimColorState = {}
---@param blendMode AnimColorBlendMode
---@return C_AnimColorState
---@overload fun(): C_AnimColorState
function C_AnimColorState.new(blendMode) end

---@class C_AnimMotionColorShift
---@field motionColor Color
---@field fadeInSpeed number
---@field fadeOutSpeed number
---@field currentBlend number
local C_AnimMotionColorShift = {}
---@param motionColor Color
---@param fadeInSpeed number
---@param fadeOutSpeed number
---@return C_AnimMotionColorShift
---@overload fun(): C_AnimMotionColorShift
function C_AnimMotionColorShift.new(motionColor, fadeInSpeed, fadeOutSpeed) end

-- ═══════════════════════════════════════════════════════════════════════════
-- IRMath  –  math utilities + layout functions
-- ═══════════════════════════════════════════════════════════════════════════

---@class IRMathLib
IRMath = {}

---@enum PlaneIso
PlaneIso = {
    XY = 0,
    XZ = 1,
    YZ = 2,
}

---@param value number
---@return number
function IRMath.fract(value) end

---@param value number
---@return number
function IRMath.clamp01(value) end

---@param a number
---@param b number
---@param t number
---@return number
function IRMath.lerp(a, b, t) end

---@param a integer
---@param b integer
---@param t number
---@return integer
function IRMath.lerpByte(a, b, t) end

---@param a Color
---@param b Color
---@param t number
---@return Color
function IRMath.lerpColor(a, b, t) end

---@param h number
---@param s number
---@param v number
---@return number r, number g, number b
function IRMath.hsvToRgb(h, s, v) end

---@param h number
---@param s number
---@param v number
---@return integer r, integer g, integer b
function IRMath.hsvToRgbBytes(h, s, v) end

---@param colors Color[]
---@return Color[]
function IRMath.sortByHue(colors) end

---@param colors Color[]
---@return Color[]
function IRMath.sortBySaturation(colors) end

---@param colors Color[]
---@return Color[]
function IRMath.sortByValue(colors) end

---@param colors Color[]
---@return Color[]
function IRMath.sortByLuminance(colors) end

---@param c Color
---@return ColorHSV
function IRMath.colorToHSV(c) end

---@param hsv ColorHSV
---@return Color
function IRMath.hsvToColor(hsv) end

---@param pos vec3
---@param depth number
---@return vec3
function IRMath.isoDepthShift(pos, depth) end

---@param filename string
---@return Color[]
function IRMath.loadPalette(filename) end

---@param index integer
---@param count integer
---@param columns integer
---@param spacingPrimary number
---@param spacingSecondary number
---@param plane PlaneIso
---@param depth number
---@return vec3
function IRMath.layoutGridCentered(index, count, columns, spacingPrimary, spacingSecondary, plane, depth) end

---@param index integer
---@param count integer
---@param itemsPerZag integer
---@param spacingPrimary number
---@param spacingSecondary number
---@param plane PlaneIso
---@param depth number
---@return vec3
function IRMath.layoutZigZagCentered(index, count, itemsPerZag, spacingPrimary, spacingSecondary, plane, depth) end

---@param index integer
---@param count integer
---@param itemsPerSegment integer
---@param spacingPrimary number
---@param spacingSecondary number
---@param plane PlaneIso
---@param depth number
---@return vec3
function IRMath.layoutZigZagPath(index, count, itemsPerSegment, spacingPrimary, spacingSecondary, plane, depth) end

---@param index integer
---@param spacing number
---@param plane PlaneIso
---@param depth number
---@return vec3
function IRMath.layoutSquareSpiral(index, spacing, plane, depth) end

---@param index integer
---@param count integer
---@param radius number
---@param turns number
---@param heightSpan number
---@param axis integer
---@return vec3
function IRMath.layoutHelix(index, count, radius, turns, heightSpan, axis) end

-- ═══════════════════════════════════════════════════════════════════════════
-- IRRender
-- ═══════════════════════════════════════════════════════════════════════════

---@class IRRenderLib
IRRender = {}

---@param zoom number
function IRRender.setCameraZoom(zoom) end

-- ═══════════════════════════════════════════════════════════════════════════
-- IREntity
-- ═══════════════════════════════════════════════════════════════════════════

---@class IREntityLib
IREntity = {}

---@param entityName string
---@return LuaEntity
function IREntity.getEntity(entityName) end

---@param canvasName string
---@return LuaEntity
function IREntity.getCanvasEntity(canvasName) end

---@param entity LuaEntity
---@return ivec3
function IREntity.getCanvasSizeTriangles(entity) end

---@param entity LuaEntity
---@param background C_TriangleCanvasBackground
function IREntity.setTriangleCanvasBackground(entity, background) end

---@param entity LuaEntity
function IREntity.clearTriangleCanvasBackground(entity) end

---@param entity LuaEntity
---@param behavior C_TrixelCanvasRenderBehavior
function IREntity.setTrixelCanvasRenderBehavior(entity, behavior) end

---@param entity LuaEntity
---@param zoomLevel C_ZoomLevel
function IREntity.setZoomLevel(entity, zoomLevel) end

---@param clip C_AnimationClip
---@return LuaEntity clipEntity
function IREntity.createAnimationClip(clip) end

---@param clip C_AnimationClip
---@param colorTrack C_AnimClipColorTrack
---@return LuaEntity clipEntity
function IREntity.createAnimationClipWithColor(clip, colorTrack) end

-- ═══════════════════════════════════════════════════════════════════════════
-- IRAudio
-- ═══════════════════════════════════════════════════════════════════════════

---@class IRAudioLib
IRAudio = {}

---@param name string
---@return integer portId
function IRAudio.openMidiOut(name) end

---@param name string
---@return integer portId
function IRAudio.openMidiIn(name) end

---@param noteName NoteName
---@param octave integer
---@return integer midiNoteNumber
function IRAudio.rootNote(noteName, octave) end

---@param mode ScaleMode
---@return integer[] intervals  Semitone offsets from root (1-indexed Lua table)
function IRAudio.getScaleIntervals(mode) end

---@param mode ScaleMode
---@return integer  Number of degrees in the scale
function IRAudio.getScaleSize(mode) end

-- ═══════════════════════════════════════════════════════════════════════════
-- IRPhysics
-- ═══════════════════════════════════════════════════════════════════════════

---@class IRPhysicsLib
IRPhysics = {}

---@param magnitude number
function IRPhysics.setGravityMagnitude(magnitude) end

---@return number
function IRPhysics.getGravityMagnitude() end

---@param gravity number
---@param height number
---@return number impulse
function IRPhysics.impulseForHeight(gravity, height) end

---@param gravity number
---@param height number
---@return number seconds
function IRPhysics.flightTimeForHeight(gravity, height) end

-- ═══════════════════════════════════════════════════════════════════════════
-- IREntity – batch entity creation functions
-- ═══════════════════════════════════════════════════════════════════════════

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchVoxelPeriodicIdle(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchPolyrhythm(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchPolyrhythmBurst(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchNoteBlocks(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchNoteBlocksPhysics(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchNotePlatforms(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchNotePlatformsAnimated(count, ...) end

---@param count ivec3
---@vararg fun(params: CreateEntityCallbackParams): any
function IREntity.createEntityBatchNotePlatformsAnimatedColor(count, ...) end

---@param ... any
---@return LuaEntity
function IREntity.createMidiSequence(...) end
