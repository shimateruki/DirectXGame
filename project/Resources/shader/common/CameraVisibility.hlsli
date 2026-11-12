#ifndef CAMERA_VISIBILITY_HLSLI
#define CAMERA_VISIBILITY_HLSLI
// Evaluate a perspective cone ending just in front of the subject. No viewport-size assumptions.
float CameraVisibilityMask(float3 worldPos, float3 normal, float3 eye, float4 centerRadius, float4 parameters)
{
    if (parameters.x <= 0.0001f) return 0.0f;
    float3 axis = centerRadius.xyz - eye;
    float distance = length(axis);
    if (distance <= parameters.w + 0.1f) return 0.0f;
    axis /= distance;
    float3 offset = worldPos - eye;
    float along = dot(offset, axis);
    float limit = distance - parameters.w;
    if (along <= 0.0f || along >= limit) return 0.0f;
    // Keep walkable surfaces beneath the subject visible, including slopes.
    if (normal.y > 0.55f && worldPos.y <= parameters.z + 0.25f) return 0.0f;
    float radius = max(centerRadius.w * along / distance, 0.001f);
    float radial = length(offset - axis * along) / radius;
    float disk = 1.0f - smoothstep(1.0f - parameters.y, 1.0f, radial);
    float depthFade = 1.0f - smoothstep(max(0.0f, limit - 0.3f), limit, along);
    return saturate(disk * depthFade * parameters.x);
}
// Ordered coverage keeps opaque depth writes and avoids transparent sorting artifacts.
void ApplyCameraVisibilityDither(float mask, float2 pixel)
{
    // Static screen-space interleaved noise avoids visible Bayer bands. All layers share
    // the same threshold, so holes align when several occluders overlap.
    float threshold = frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f))));
    clip(1.0f - mask - threshold);
}
#endif
