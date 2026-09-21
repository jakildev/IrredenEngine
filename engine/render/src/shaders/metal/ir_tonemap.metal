// ACES Filmic tone mapping (Stephen Hill's fitted curve).
// Maps [0, ∞) → [0, 1) with a gentle shoulder that preserves color
// saturation in bright highlights better than Reinhard.
float3 ACESFilm(float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}
