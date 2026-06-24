#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
Texture2D<float4> bakedFireFlameTex : register(t2);
Texture2D<float4> bakedFireOrbTex : register(t3);
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

    float a = Hash12(i + float2(0.0f, 0.0f));
    float b = Hash12(i + float2(1.0f, 0.0f));
    float c = Hash12(i + float2(0.0f, 1.0f));
    float d = Hash12(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm2(float2 p)
{
    float value = 0.0f;
    float amp = 0.5f;

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.02f + float2(19.13f, 31.71f);
        amp *= 0.5f;
    }

    return value;
}

float LinearizeDepth(float z)
{
    z = saturate(z);
    return (kNearClip * kFarClip) / max(kFarClip - z * (kFarClip - kNearClip), 0.0001f);
}

float3 TonalFireRamp(float heat, float ember, float3 artistTint)
{
    float3 deepCoal = float3(0.060f, 0.006f, 0.002f);
    float3 redCore = lerp(float3(0.95f, 0.040f, 0.008f), artistTint, 0.18f);
    float3 orange = float3(2.10f, 0.50f, 0.055f);
    float3 yellow = float3(3.35f, 1.55f, 0.23f);
    float3 whiteHot = float3(4.65f, 3.65f, 1.70f);

    float3 c = deepCoal;
    c = lerp(c, redCore, smoothstep(0.18f, 0.22f, heat));
    c = lerp(c, orange, smoothstep(0.42f, 0.47f, heat));
    c = lerp(c, yellow, smoothstep(0.64f, 0.69f, heat));
    c = lerp(c, whiteHot, smoothstep(0.86f, 0.91f, heat));
    c += float3(1.0f, 0.22f, 0.025f) * ember * 0.55f;
    return c;
}

float2 BuildSurfaceUV(float3 worldPos, float3 normal)
{
    float3 n = abs(normalize(normal));
    n = max(n, float3(0.001f, 0.001f, 0.001f));
    n /= (n.x + n.y + n.z);

    float2 uvX = float2(worldPos.z, worldPos.y);
    float2 uvY = float2(worldPos.x, worldPos.z);
    float2 uvZ = float2(worldPos.x, worldPos.y);
    return uvX * n.x + uvY * n.y + uvZ * n.z;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float animSpeed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float patternScale = max(effectScale, 0.05f);

    float3 centerWorld = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), world).xyz;
    float3 cameraToCenter = centerWorld - cameraWorldPosition;
    float cameraDistance = max(length(cameraToCenter), 0.0001f);
    float3 viewForward = cameraToCenter / cameraDistance;
    float3 upSeed = (abs(viewForward.y) > 0.96f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 viewRight = normalize(cross(upSeed, viewForward));
    float3 viewUp = normalize(cross(viewForward, viewRight));

    float3 axisX = float3(world._11, world._12, world._13);
    float3 axisY = float3(world._21, world._22, world._23);
    float3 axisZ = float3(world._31, world._32, world._33);
    float proxyRadius = max(max(length(axisX), length(axisY)), length(axisZ));
    proxyRadius *= max(billboardScale, 0.05f) * patternScale;
    proxyRadius = max(proxyRadius, 0.001f);

    float3 toPixel = input.worldPos - centerWorld;
    float2 billboard = float2(dot(toPixel, viewRight), dot(toPixel, viewUp)) / proxyRadius;
    float proxyMask = 1.0f;
    float2 proceduralUV = billboard * 0.5f + 0.5f;
    float2 surfaceUV = proceduralUV * 2.0f - 1.0f;

    float heat = 0.0f;
    float core = 0.0f;
    float edgeGlow = 0.0f;
    float ember = 0.0f;
    float smoke = 0.0f;
    float alpha = 0.0f;

    if (effectType < 0.5f)
    {
        float height01 = saturate(proceduralUV.y);
        float fromBase = 1.0f - height01;

        float side = abs(billboard.x);

        float2 flameUV = float2(
            (billboard.x * 0.5f + 0.5f) * (3.2f + detail * 0.8f),
            height01 * (4.0f + detail * 1.4f) - time * animSpeed * 1.15f
        );
        float bodyNoise = Fbm2(flameUV + float2(time * 0.10f, 0.0f));
        float tongueNoise = Fbm2(float2(side * 5.8f + bodyNoise * 2.2f, height01 * 8.5f - time * animSpeed * 2.0f));
        float emberNoise = Fbm2(float2(proceduralUV.x * 19.0f + time * 0.35f, height01 * 18.0f - time * animSpeed * 3.0f));
        float4 bakedFlame = bakedFireFlameTex.Sample(smp, saturate(proceduralUV + float2((bodyNoise - 0.5f) * 0.04f, 0.0f)));

        float width = lerp(0.92f, 0.14f, smoothstep(0.06f, 1.0f, height01));
        width += fromBase * 0.12f;
        width += (bodyNoise - 0.5f) * 0.12f;

        float edge = side + (tongueNoise - 0.5f) * lerp(0.14f, 0.34f, height01);
        float edgeSoft = lerp(0.055f, 0.22f, softness);
        float flameMask = smoothstep(width + edgeSoft, width - edgeSoft, edge);
        flameMask *= smoothstep(0.00f, 0.08f, height01);
        flameMask *= lerp(1.0f, 0.58f, smoothstep(0.88f, 1.0f, height01));
        flameMask = saturate(flameMask * 0.72f + bakedFlame.a * 0.42f);

        float inner = saturate(1.0f - side);
        heat = saturate(0.20f + fromBase * 0.20f + inner * 0.18f + bodyNoise * 0.28f + tongueNoise * 0.30f + bakedFlame.r * 0.24f);
        core = smoothstep(0.58f, 0.92f, heat + inner * 0.18f + bakedFlame.g * 0.16f - height01 * 0.06f) * flameMask;
        edgeGlow = smoothstep(0.40f, 0.92f, tongueNoise + bodyNoise * 0.28f + bakedFlame.r * 0.28f) * flameMask;
        ember = smoothstep(0.78f, 0.96f, emberNoise + fromBase * 0.12f + bakedFlame.b * 0.28f) * flameMask;
        smoke = smoothstep(0.62f, 0.96f, height01) * smoothstep(0.35f, 0.86f, 1.0f - bodyNoise) * flameMask;
        alpha = saturate(flameMask * proxyMask * (0.30f + heat * 0.50f + core * 0.26f));
    }
    else if (effectType < 1.5f)
    {
        float radial = length(billboard);
        float2 orbUV = billboard * (2.2f + detail * 0.42f);
        float bodyNoise = Fbm2(orbUV + float2(time * 0.18f, -time * animSpeed * 0.44f));
        float broadNoise = Fbm2(orbUV * 0.48f + float2(-time * 0.07f, time * 0.11f));
        float crackNoise = Fbm2(orbUV * 3.4f + float2(time * 0.46f, -time * animSpeed * 0.96f));
        float pulse = sin(time * animSpeed * 2.4f + bodyNoise * 5.2f) * 0.5f + 0.5f;
        float edgeSoft = lerp(0.035f, 0.20f, softness);
        float4 bakedOrb = bakedFireOrbTex.Sample(smp, saturate(proceduralUV + (bodyNoise - 0.5f) * 0.035f));
        float sphereMask = smoothstep(1.0f + edgeSoft, 1.0f - edgeSoft, radial + (bodyNoise - 0.5f) * 0.10f);
        sphereMask = saturate(sphereMask * 0.76f + bakedOrb.a * 0.34f);

        float patch = smoothstep(0.30f, 0.84f, bodyNoise + crackNoise * 0.28f);
        heat = saturate(0.18f + broadNoise * 0.18f + bodyNoise * 0.24f + crackNoise * 0.18f + pulse * 0.10f + bakedOrb.r * 0.28f);
        core = smoothstep(0.58f, 0.90f, heat + patch * 0.16f + bakedOrb.g * 0.18f) * sphereMask;
        edgeGlow = smoothstep(0.60f, 0.94f, crackNoise + bodyNoise * 0.20f + bakedOrb.r * 0.26f) * sphereMask;
        ember = smoothstep(0.76f, 0.96f, crackNoise + pulse * 0.22f + bakedOrb.b * 0.22f) * sphereMask;
        smoke = smoothstep(0.58f, 0.94f, 1.0f - broadNoise) * smoothstep(0.22f, 0.82f, heat) * sphereMask;
        alpha = saturate((0.24f + patch * 0.38f + heat * 0.22f + core * 0.18f) * sphereMask * proxyMask);
    }
    else if (effectType < 2.5f)
    {
        float wind = clamp(flowSpeedX, -1.0f, 1.0f);
        float move = saturate(abs(flowSpeedY));
        float height01 = saturate(proceduralUV.y);
        float fromBase = 1.0f - height01;
        float sx = max(effectScaleX, 0.12f);
        float sy = max(effectScaleY, 0.12f);

        float lean = wind * (0.10f + move * 0.22f) * smoothstep(0.16f, 1.0f, height01);
        float2 wrapBillboard = float2((billboard.x + lean) / sx, (billboard.y + 0.08f) / sy);
        float oval = length(float2(wrapBillboard.x * 0.86f, wrapBillboard.y * 1.08f));

        float2 wrapUV = float2(
            wrapBillboard.x * (4.0f + detail * 0.75f) + wind * time * (0.22f + move * 0.18f),
            height01 * (4.8f + detail * 1.0f) - time * animSpeed * (0.70f + move * 0.42f)
        );
        float bodyNoise = Fbm2(wrapUV + float2(time * 0.08f, 0.0f));
        float tongueNoise = Fbm2(float2(abs(wrapBillboard.x) * 5.6f + bodyNoise * 1.8f, height01 * 7.4f - time * animSpeed * (1.55f + move * 0.65f)));
        float emberNoise = Fbm2(float2(proceduralUV.x * 15.0f + time * 0.28f, height01 * 13.0f - time * animSpeed * 2.1f));
        float4 bakedFlame = bakedFireFlameTex.Sample(smp, saturate(proceduralUV + float2((bodyNoise - 0.5f) * 0.035f + lean * 0.08f, 0.0f)));

        float bodyShell = smoothstep(1.10f, 0.58f, oval + (bodyNoise - 0.5f) * 0.11f);
        float topFade = smoothstep(1.08f, 0.54f, height01 + (tongueNoise - 0.5f) * 0.10f);
        float bottomFade = smoothstep(0.02f, 0.17f, height01);
        float sideLick = smoothstep(0.88f, 0.45f, abs(wrapBillboard.x) + (tongueNoise - 0.5f) * 0.22f);
        float flameMask = saturate(bodyShell * bottomFade * topFade);
        flameMask = saturate(flameMask * (0.46f + sideLick * 0.34f) + bakedFlame.a * 0.18f);

        float inner = saturate(1.0f - oval);
        heat = saturate(0.16f + fromBase * 0.12f + inner * 0.16f + bodyNoise * 0.26f + tongueNoise * 0.22f + bakedFlame.r * 0.20f + move * 0.08f);
        core = smoothstep(0.66f, 0.94f, heat + sideLick * 0.12f + bakedFlame.g * 0.11f) * flameMask;
        edgeGlow = smoothstep(0.52f, 0.92f, tongueNoise + bodyNoise * 0.22f + bakedFlame.r * 0.20f) * flameMask;
        ember = smoothstep(0.80f, 0.97f, emberNoise + bakedFlame.b * 0.20f + move * 0.06f) * flameMask;
        smoke = smoothstep(0.70f, 0.98f, height01) * smoothstep(0.44f, 0.88f, 1.0f - bodyNoise) * flameMask;
        alpha = saturate(flameMask * proxyMask * (0.18f + heat * 0.34f + core * 0.18f));
    }
    else
    {
        float wind = clamp(flowSpeedX, -1.0f, 1.0f);
        float stream = saturate(abs(flowSpeedY));
        float sx = max(effectScaleX, 0.12f);
        float sy = max(effectScaleY, 0.12f);
        float2 plume = float2((billboard.x + wind * 0.12f) / sx, billboard.y / sy);
        float radial = length(float2(plume.x * 0.88f, plume.y * 1.18f));
        float front = saturate(proceduralUV.x);
        float centerBand = saturate(1.0f - abs(plume.y) * 1.8f);

        float2 plumeUV = float2(
            plume.x * (3.2f + detail * 0.7f) + time * (0.28f + stream * 0.22f) + wind * 0.35f,
            plume.y * (4.6f + detail * 0.8f) - time * animSpeed * (1.25f + stream * 0.55f)
        );
        float bodyNoise = Fbm2(plumeUV);
        float lickNoise = Fbm2(plumeUV * 1.85f + float2(time * 0.22f, -time * animSpeed * 0.70f));
        float emberNoise = Fbm2(plumeUV * 3.2f + float2(time * 0.55f, -time * 0.80f));
        float4 bakedFlame = bakedFireFlameTex.Sample(smp, saturate(proceduralUV + float2((bodyNoise - 0.5f) * 0.05f + wind * 0.04f, (lickNoise - 0.5f) * 0.035f)));

        float shell = smoothstep(1.18f, 0.56f, radial + (bodyNoise - 0.5f) * 0.18f);
        float tornEdge = smoothstep(0.40f, 0.92f, lickNoise + centerBand * 0.20f + bakedFlame.r * 0.18f);
        float flameMask = saturate(shell * (0.54f + tornEdge * 0.42f) + bakedFlame.a * 0.20f);
        flameMask *= smoothstep(0.02f, 0.18f, front);
        flameMask *= lerp(0.84f, 1.0f, centerBand);

        heat = saturate(0.22f + centerBand * 0.18f + bodyNoise * 0.28f + lickNoise * 0.24f + bakedFlame.r * 0.22f + stream * 0.08f);
        core = smoothstep(0.58f, 0.91f, heat + centerBand * 0.16f + bakedFlame.g * 0.12f) * flameMask;
        edgeGlow = smoothstep(0.48f, 0.92f, lickNoise + bodyNoise * 0.22f + bakedFlame.r * 0.18f) * flameMask;
        ember = smoothstep(0.78f, 0.97f, emberNoise + bakedFlame.b * 0.20f + stream * 0.08f) * flameMask;
        smoke = smoothstep(0.58f, 0.94f, 1.0f - bodyNoise) * smoothstep(0.14f, 0.90f, radial) * flameMask;
        alpha = saturate(flameMask * proxyMask * (0.26f + heat * 0.38f + core * 0.20f));
    }

    float bgDepth = depthTex.SampleLevel(smp, screenUV, 0).r;
    float fireDepth = input.screenPos.z / input.screenPos.w;
    float depthDiff = LinearizeDepth(bgDepth) - LinearizeDepth(fireDepth);
    float softFactor = lerp(0.68f, 1.0f, saturate(depthDiff / 0.36f));
    softFactor = (bgDepth >= 0.999f) ? 1.0f : softFactor;

    float2 distortionNoise;
    distortionNoise.x = Fbm2(surfaceUV * 2.1f + float2(time * 0.23f, -time * 1.25f));
    distortionNoise.y = Fbm2(surfaceUV * 2.4f + float2(4.3f - time * 0.18f, -time * 1.05f));
    distortionNoise = distortionNoise * 2.0f - 1.0f;

    float heatDistortion = (0.004f + 0.010f * heat) * saturate(0.35f + core + edgeGlow * 0.45f);
    float2 distortedUV = saturate(screenUV + distortionNoise * heatDistortion * softFactor);
    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 distortedScene = grabTex.SampleLevel(smp, distortedUV, 0).rgb;

    float3 artistTint = max(color.rgb, float3(0.08f, 0.02f, 0.005f));
    float3 fireColor = TonalFireRamp(heat, ember, artistTint);

    fireColor += float3(1.25f, 0.28f, 0.035f) * edgeGlow * 0.55f;
    fireColor += float3(2.0f, 0.75f, 0.12f) * core * 0.28f;
    fireColor *= intensity;

    float3 smokeColor = lerp(float3(0.105f, 0.044f, 0.018f), float3(0.032f, 0.025f, 0.020f), heat);
    fireColor = lerp(fireColor, smokeColor, smoke * 0.18f);

    alpha *= softFactor * color.a;
    if (alpha < 0.012f)
    {
        discard;
    }

    float distortMix = saturate((0.18f + heat * 0.28f + smoke * 0.16f) * softFactor);
    float3 background = lerp(sceneColor, distortedScene, distortMix);
    float3 source = lerp(background, fireColor, saturate(alpha * 0.92f + 0.16f));
    source += fireColor * (0.12f + core * 0.24f + ember * 0.12f);

    return float4(source, alpha);
}
