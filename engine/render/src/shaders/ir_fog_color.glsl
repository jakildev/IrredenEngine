// Fog BODY colour mapping. Keep the Metal twin normalized equal.

const float kFogExploredValue = 128.0 / 255.0;

vec3 fogStateColor(float state, vec3 sourceColor, vec3 unexplored) {
    const float luminance = dot(sourceColor, vec3(0.299, 0.587, 0.114));
    const vec3 exploredColor = vec3(luminance) * 0.4;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0 - kFogExploredValue);
        return mix(exploredColor, sourceColor, t);
    }
    const float t = state / kFogExploredValue;
    return mix(unexplored, exploredColor, t);
}
