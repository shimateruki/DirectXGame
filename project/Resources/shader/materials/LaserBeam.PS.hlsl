#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

static const float kNearClip = 0.1f;
static const float kFarClip = 1000.0f;

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);

    float a = Hash12(i);
    float b = Hash12(i + float2(1.0f, 0.0f));
    float c = Hash12(i + float2(0.0f, 1.0f));
    float d = Hash12(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float LinearizeDepth(float z)
{
    z = saturate(z);
    return (kNearClip * kFarClip) / max(kFarClip - z * (kFarClip - kNearClip), 0.0001f);
}

void BuildBeamFrame(out float3 axisDir, out float3 sideA, out float3 sideB, out float axisLength, out float thickness)
{
    float3 axisX = float3(world._11, world._12, world._13);
    float3 axisY = float3(world._21, world._22, world._23);
    float3 axisZ = float3(world._31, world._32, world._33);

    float lenX = max(length(axisX), 0.0001f);
    float lenY = max(length(axisY), 0.0001f);
    float lenZ = max(length(axisZ), 0.0001f);

    axisDir = axisX / lenX;
    sideA = axisY / lenY;
    sideB = axisZ / lenZ;
    axisLength = lenX;
    thickness = max(lenY, lenZ);

    if (lenY > axisLength)
    {
        axisDir = axisY / lenY;
        sideA = axisX / lenX;
        sideB = axisZ / lenZ;
        axisLength = lenY;
        thickness = max(lenX, lenZ);
    }

    if (lenZ > axisLength)
    {
        axisDir = axisZ / lenZ;
        sideA = axisX / lenX;
        sideB = axisY / lenY;
        axisLength = lenZ;
        thickness = max(lenX, lenY);
    }

    sideA = normalize(sideA - axisDir * dot(sideA, axisDir));
    sideB = normalize(cross(axisDir, sideA));
    thickness = max(thickness, 0.0001f);
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float patternScale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);

    float3 axisDir;
    float3 sideA;
    float3 sideB;
    float axisLength;
    float thickness;
    BuildBeamFrame(axisDir, sideA, sideB, axisLength, thickness);

    float3 centerWorld = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), world).xyz;
    float3 toPixel = input.worldPos - centerWorld;
    float axisCoord = dot(toPixel, axisDir);
    float3 radialVec = toPixel - axisDir * axisCoord;
    float radial = length(radialVec) / thickness;
    float axis01 = axisCoord / axisLength + 0.5f;
    float angle = atan2(dot(radialVec, sideB), dot(radialVec, sideA));

    float shellNoise = Noise2D(float2(axis01 * (32.0f + detail * 3.0f) - time * speed * 5.0f, angle * 1.7f));
    float scan = 0.5f + 0.5f * sin(axis01 * (72.0f + patternScale * 22.0f) - time * speed * 13.0f + shellNoise * 2.2f);
    float spiralA = 0.5f + 0.5f * sin(angle * 3.0f + axis01 * (28.0f + detail) - time * speed * 8.0f);
    float spiralB = 0.5f + 0.5f * sin(-angle * 4.0f + axis01 * (20.0f + detail * 1.6f) + time * speed * 6.0f);

    float edgeSoft = lerp(0.018f, 0.13f, softness);
    float core = 1.0f - smoothstep(0.0f, 0.16f + edgeSoft, radial);
    float inner = 1.0f - smoothstep(0.12f, 0.34f + edgeSoft, radial);
    float glow = 1.0f - smoothstep(0.22f, 1.28f + edgeSoft, radial);
    float shell = smoothstep(0.36f, 0.56f, radial) * (1.0f - smoothstep(0.82f, 1.18f, radial));
    shell *= saturate(spiralA * 0.36f + spiralB * 0.28f + shellNoise * 0.16f);

    float endFade = smoothstep(0.0f, 0.04f, axis01) * (1.0f - smoothstep(0.96f, 1.0f, axis01));
    float scanLine = pow(scan, 5.0f) * inner;
    float energy = saturate(core * (1.95f + scan * 0.35f) + scanLine * 1.15f + glow * 0.38f + shell * 0.42f) * endFade;

    float bgDepth = depthTex.SampleLevel(smp, screenUV, 0).r;
    float beamDepth = input.screenPos.z / input.screenPos.w;
    float depthDiff = LinearizeDepth(bgDepth) - LinearizeDepth(beamDepth);
    float depthFade = (bgDepth >= 0.999f) ? 1.0f : saturate(depthDiff / 0.65f + 0.45f);
    energy *= depthFade;

    float3 tint = lerp(float3(0.12f, 0.78f, 1.75f), saturate(color.rgb), 0.58f);
    float3 hotCore = float3(3.2f, 3.7f, 4.0f);
    float3 beamColor = tint * (glow * 1.85f + shell * 0.95f + scanLine * 1.2f) + hotCore * core * 3.4f;
    beamColor *= intensity;

    float alpha = saturate(color.a * energy);
    if (alpha < 0.006f)
    {
        discard;
    }

    return float4(beamColor, alpha);
}
