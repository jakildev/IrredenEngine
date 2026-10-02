#ifndef IR_FOG_COLOR_METAL_INCLUDED
#define IR_FOG_COLOR_METAL_INCLUDED

constant float kFogExploredValue = 128.0f / 255.0f;

inline float3 fogStateColor(float state, float3 sourceColor, float3 unexplored) {
    const float luminance = dot(sourceColor, float3(0.299f, 0.587f, 0.114f));
    const float3 exploredColor = float3(luminance) * 0.4f;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0f - kFogExploredValue);
        return mix(exploredColor, sourceColor, t);
    }
    const float t = state / kFogExploredValue;
    return mix(unexplored, exploredColor, t);
}

#endif // IR_FOG_COLOR_METAL_INCLUDED
