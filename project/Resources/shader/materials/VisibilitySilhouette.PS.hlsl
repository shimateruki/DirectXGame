#include "../common/Water.hlsli"
Texture2D<float> depthTex : register(t0);
float4 main(VSOutput input) : SV_TARGET
{
    float2 uv = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    uint width, height;
    depthTex.GetDimensions(width, height);
    uint2 pixel = min(uint2(saturate(uv) * float2(width, height)), uint2(width - 1, height - 1));
    float sceneDepth = depthTex.Load(int3(pixel, 0));
    float subjectDepth = input.screenPos.z / input.screenPos.w;
    // Visible parts and the sky do not receive an overlay.
    clip(subjectDepth - sceneDepth - 0.00001f);
    float4 sceneWorld = mul(float4(uv * float2(2, -2) + float2(-1, 1), sceneDepth, 1), waterInverseViewProjection);
    sceneWorld.xyz /= sceneWorld.w;
    float gap = length(input.worldPos - cameraWorldPosition) - length(sceneWorld.xyz - cameraWorldPosition);
    // Reject tiny self-occlusion gaps and ease in behind genuinely separate geometry.
    clip(gap - 0.12f);
    float rim = pow(1.0f - saturate(abs(dot(normalize(input.normal), normalize(cameraWorldPosition - input.worldPos)))), 2.0f);
    float alpha = visibilityParameters.x * lerp(0.45f, 1.0f, rim) * smoothstep(0.12f, 0.4f, gap);
    return float4(waterLightColor, alpha);
}
