// [0] WVP constants. Must match the C++ WVP buffer.
cbuffer cbWVP : register(b0)
{
    matrix WVP;
    matrix world;
    matrix WorldInverseTranspose;
};

// [1] Shared effect parameters. Must match MeshRenderer::WaterParamForGPU.
cbuffer cbWaterParam : register(b1)
{
    float time;
    float waveSpeed;
    float waveHeight;
    float waveFrequency;
    float flowSpeedX;
    float flowSpeedY;
    float uvOffsetX;
    float uvOffsetY;
    float effectType;
    float effectScale;
    float effectSoftness;
    float effectIntensity;
    float3 cameraWorldPosition;
    float billboardScale;
    float effectScaleX;
    float effectScaleY;
    float effectScaleZ;
    float topDownDetail;
    float3 waterLightDirection;
    float waterLightIntensity;
    float3 waterLightColor;
    float waterLinearWorkflowEnabled;
    matrix waterInverseViewProjection;
    // Runtime-only water diagnostics; not part of saved material settings.
    float waterDebugView;
    float3 waterDebugPadding;
};

// [2] Material color from the inspector.
cbuffer cbMaterial : register(b2)
{
    float4 color;
};

struct VSInput
{
    float4 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float3 localPos : TEXCOORD2;
    float4 screenPos : TEXCOORD1;
};

// Height and analytic derivatives share the same phases in both shader stages.
// Only long waves displace the grid; short waves are filtered pixel normals.
float3 WaterSwell(float2 p)
{
    float scale = max(waveFrequency, 0.1f) / 5.4f;
    float t = time * max(waveSpeed, 0.0f);
    float2 k0 = float2(0.065f, 0.024f) * scale;
    // A dominant travelling swell with a weaker crossing component, not an equal grid.
    float2 k1 = float2(0.041f, 0.025f) * scale;
    float2 k2 = float2(-0.022f, 0.087f) * scale;
    float3 phase = float3(dot(p, k0) - t * 1.3f,
        dot(p, k1) - t * 1.7f + 1.2f, dot(p, k2) - t * 2.1f + 2.4f);
    // Slowly bend crest lines; keep exact phase gradients for matching vertex/pixel normals.
    float2 bendK0 = float2(-0.011f, 0.017f) * scale;
    float2 bendK1 = float2(0.019f, -0.009f) * scale;
    float bend0 = dot(p, bendK0) - t * 0.31f;
    float bend1 = dot(p, bendK1) - t * 0.23f;
    phase.x += 0.75f * sin(bend0);
    phase.y += 0.55f * sin(bend1);
    k0 += 0.75f * cos(bend0) * bendK0;
    k1 += 0.55f * cos(bend1) * bendK1;
    float3 amplitude = max(waveHeight, 0.0f) * float3(0.65f, 0.25f, 0.10f);
    // The second harmonic sharpens crests and broadens troughs without shifting mean height.
    // Normalize the profile so Wave Height remains a conservative displacement bound.
    float3 profile = (sin(phase) - 0.18f * cos(2.0f * phase)) / 1.18f;
    float3 c = (cos(phase) + 0.36f * sin(2.0f * phase)) * amplitude / 1.18f;
    return float3(dot(profile, amplitude), k0 * c.x + k1 * c.y + k2 * c.z);
}
