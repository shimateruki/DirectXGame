#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
Texture2D<float4> bakedGateSwirlTex : register(t2);
SamplerState smp : register(s0);

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

float Fbm2(float2 p)
{
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.03f + float2(19.7f, 7.3f);
        amp *= 0.52f;
    }
    return value;
}

float RingMask(float value, float center, float width)
{
    float halfWidth = max(width, 0.001f) * 0.5f;
    return 1.0f - smoothstep(halfWidth * 0.25f, halfWidth, abs(value - center));
}

float ArcMask(float angle, float radius, float arcCount, float scroll, float sharpness)
{
    float wave = sin(angle * arcCount + radius * (10.0f + arcCount * 1.7f) + scroll);
    return pow(saturate(wave * 0.5f + 0.5f), sharpness);
}

float SparkField(float2 p, float t, float density, float threshold)
{
    float2 cell = floor(p * density);
    float2 local = frac(p * density) - 0.5f;
    float seed = Hash12(cell);
    float sparkle = smoothstep(threshold, 1.0f, seed);
    float twinkle = 0.55f + 0.45f * sin(t * (2.2f + seed * 4.8f) + seed * 19.0f);
    float shape = exp(-dot(local, local) * (28.0f + seed * 30.0f));
    return sparkle * twinkle * shape;
}

float LightningBranch(float angle, float radius, float t, float count, float phase, float sharpness)
{
    float bend = sin(radius * 25.0f + t * 2.7f + phase) * 0.38f;
    bend += sin(radius * 57.0f - t * 4.1f + phase * 1.7f) * 0.16f;
    float ray = sin(angle * count + radius * (18.0f + count) + bend + phase - t * 5.0f);
    float radialGate = smoothstep(0.12f, 0.24f, radius) * (1.0f - smoothstep(0.68f, 0.88f, radius));
    return pow(saturate(ray * 0.5f + 0.5f), sharpness) * radialGate;
}

float TunnelRings(float radius, float angle, float t, float activation)
{
    float rings = 0.0f;

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float fi = (float)i;
        float depth01 = fi / 4.0f;
        float ringRadius = 0.205f + depth01 * 0.500f + sin(t * (0.42f + depth01 * 0.16f) + fi * 1.73f) * 0.010f;
        float ringWidth = lerp(0.026f, 0.055f, depth01);
        float brokenArc = 0.58f + 0.42f * sin(angle * (8.0f + fi * 2.0f) - t * (1.35f + fi * 0.18f) + fi * 2.1f);
        brokenArc = pow(saturate(brokenArc), lerp(2.6f, 5.0f, depth01));
        float depthFade = lerp(0.36f, 1.0f, depth01);
        rings += RingMask(radius, ringRadius, ringWidth) * (0.48f + brokenArc * 0.72f) * depthFade;
    }

    return saturate(rings) * activation;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float depthPower = max(waveHeight, 0.05f);
    float softness = saturate(effectSoftness);
    float rawIntensity = max(effectIntensity, 0.0f);
    float activation = saturate((rawIntensity - 0.06f) / 1.46f);
    activation = activation * activation * (3.0f - 2.0f * activation);
    activation = max(activation, saturate(uvOffsetY));
    float activationBurst = saturate(uvOffsetX);
    float intensity = max(rawIntensity, 0.05f);
    float mode = effectType;
    float t = time * speed * lerp(0.24f, 1.12f, activation);

    float2 p = input.localPos.xy;
    float quadEdge = max(abs(p.x), abs(p.y));
    float quadSafeFade = 1.0f - smoothstep(0.68f, 0.94f, quadEdge);
    float portalAspect = 1.0f;
    p.x *= portalAspect;
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float2 radialDir = p / max(radius, 0.0001f);
    float2 tangentDir = float2(-radialDir.y, radialDir.x);
    float2 gateMaskUV = saturate(float2(p.x / portalAspect, p.y) * 0.5f + 0.5f);
    float4 bakedSwirl = bakedGateSwirlTex.Sample(smp, gateMaskUV);

    float outerRadius = 0.720f + softness * 0.020f;
    float outerClip = (1.0f - smoothstep(outerRadius - 0.020f, outerRadius + 0.035f, radius)) * quadSafeFade;
    float coreHole = smoothstep(0.135f, 0.42f, radius);
    float portalMask = outerClip * saturate(coreHole + 0.13f) * lerp(0.22f, 1.0f, activation);
    float shellDepth = RingMask(radius, 0.745f + softness * 0.020f, 0.260f) * outerClip * activation;
    float innerDepthShade = smoothstep(0.20f, 0.58f, radius) * (1.0f - smoothstep(0.76f, 0.96f, radius)) * portalMask * activation;
    float throatShade = exp(-radius * radius * 4.8f) * portalMask * activation;
    float entrySeal = exp(-radius * radius * 2.9f) * portalMask * activation;

    float rimWide = RingMask(radius, outerRadius - 0.045f, 0.115f + softness * 0.045f) * outerClip;
    float rimHot = RingMask(radius, outerRadius - 0.030f, 0.050f + softness * 0.025f) * outerClip;
    float rimInner = RingMask(radius, 0.705f + sin(t * 0.55f) * 0.018f, 0.055f) * portalMask;
    float depthCore = pow(saturate(1.0f - radius * 1.36f), 2.15f + depthPower * 0.70f);
    float innerVoid = pow(saturate(1.0f - radius * 1.95f), 1.8f);

    float spiralA = angle * 0.70f + radius * (2.65f + effectScale * 1.75f);
    float spiralB = angle * -0.38f + radius * (4.75f + effectScale * 2.10f);
    float2 flowUV = float2(spiralA - t * 0.245f, spiralB + t * 0.115f);
    float coarse = Fbm2(flowUV * (1.00f + detail * 0.060f));
    float midNoise = Fbm2(flowUV * (2.45f + detail * 0.130f) + float2(t * 0.27f, -t * 0.19f));
    float fine = Fbm2(flowUV * (5.10f + detail * 0.160f) + float2(-t * 0.42f, t * 0.31f));

    float bakedLine = saturate((bakedSwirl.r - 0.22f) * 1.28f);
    float armCount = 5.0f + floor(detail * 0.055f);
    float broadArms = ArcMask(angle, radius, armCount, -t * 2.20f + coarse * 3.5f, 2.85f) * portalMask;
    float counterArms = ArcMask(angle, radius, armCount * 0.65f + 2.0f, t * 1.44f + midNoise * 3.9f, 4.20f) * portalMask;
    float fineFilaments = ArcMask(angle, radius, armCount * 1.85f + 1.0f, -t * 3.10f + fine * 5.0f, 13.5f) * portalMask;
    fineFilaments = saturate(fineFilaments * (0.28f + midNoise * 0.46f) + bakedLine * 0.08f);
    float softGateVeil = (1.0f - smoothstep(0.18f, 0.90f, radius)) * portalMask * (0.38f + coarse * 0.34f);
    float pearlyBloom = exp(-radius * radius * 2.25f) * portalMask * activation * (0.42f + 0.38f * midNoise);
    float petalRibbons =
        ArcMask(angle, radius, 6.0f, -t * 1.12f + coarse * 2.1f, 3.4f) *
        smoothstep(0.16f, 0.34f, radius) *
        (1.0f - smoothstep(0.68f, 0.88f, radius)) *
        portalMask;

    float innerBand = RingMask(radius, 0.485f + sin(t * 0.77f + coarse) * 0.018f, 0.055f) * portalMask;
    float outerRunes = RingMask(radius, 0.805f, 0.045f) * ArcMask(angle, radius, 18.0f, t * 1.65f, 18.0f) * outerClip;
    float sparkInside = SparkField(gateMaskUV + radialDir * 0.060f + tangentDir * t * 0.018f, t, 9.0f, 0.83f) * portalMask * activation;
    float sparkRim = SparkField(gateMaskUV * 1.4f + float2(t * 0.025f, -t * 0.015f), t, 13.0f, 0.89f) * rimWide * activation;
    float pullZone = smoothstep(outerRadius - 0.04f, outerRadius + 0.10f, radius) * (1.0f - smoothstep(1.17f, 1.34f, radius)) * quadSafeFade;
    float2 pullUV = gateMaskUV * 1.75f + radialDir * (t * 0.115f) + tangentDir * (t * 0.055f);
    float pullSpark = SparkField(pullUV, t, 12.0f, 0.865f) * pullZone * activation;
    float pullStreak = pow(saturate(ArcMask(angle, radius, 10.0f, -t * 3.2f + coarse * 2.4f, 9.0f)), 1.2f) * pullZone * activation;
    float glintA = exp(-dot(p - float2(-0.22f, 0.31f), p - float2(-0.22f, 0.31f)) * 58.0f) * outerClip;
    float glintB = exp(-dot(p - float2(0.32f, -0.18f), p - float2(0.32f, -0.18f)) * 70.0f) * portalMask;
    float coreRing = RingMask(radius, 0.275f + sin(t * 1.15f) * 0.020f, 0.145f) * portalMask * activation;
    float corePulse = exp(-radius * radius * 8.0f) * (0.55f + 0.45f * sin(t * 3.4f + coarse * 5.0f)) * activation;
    float shockProgress = 1.0f - activationBurst;
    float shockRadius = lerp(0.18f, 1.08f, shockProgress);
    float shockRing = RingMask(radius, shockRadius, lerp(0.135f, 0.060f, shockProgress)) * activationBurst;
    float shockHalo = pow(saturate(1.0f - abs(radius - shockRadius) / 0.30f), 2.6f) * activationBurst;
    float boltA = LightningBranch(angle, radius, t, 8.0f, 0.0f, 19.0f) * activation;
    float boltB = LightningBranch(angle, radius, t, 11.0f, 2.1f, 24.0f) * activation;
    float boltC = LightningBranch(angle, radius, t, 15.0f, 4.3f, 30.0f) * activation;
    float electricWeb = saturate(coreRing * 0.48f + boltA * 0.72f + boltB * 0.56f + boltC * 0.40f);
    float spaceMask = (1.0f - smoothstep(0.68f, 0.96f, radius)) * outerClip * activation;
    float deepInterior = (1.0f - smoothstep(0.78f, 1.00f, radius)) * outerClip * activation;
    float eventHorizon = exp(-radius * radius * 9.5f) * activation * 0.55f;
    float tunnelShaft = smoothstep(0.08f, 0.22f, radius) * (1.0f - smoothstep(0.80f, 0.98f, radius)) * outerClip * activation;
    float tunnelRings = TunnelRings(radius, angle, t, activation) * tunnelShaft;
    float spaceNebula = Fbm2(gateMaskUV * 3.0f + float2(t * 0.020f, -t * 0.026f));
    float distantStars = SparkField(gateMaskUV + float2(t * 0.012f, -t * 0.018f), t, 20.0f, 0.925f) * spaceMask;
    float depthMotes = SparkField(gateMaskUV * 1.08f + radialDir * (0.14f - t * 0.025f) + tangentDir * t * 0.022f, t, 16.0f, 0.840f) * deepInterior;
    float rimMotes = SparkField(gateMaskUV * 1.45f + radialDir * 0.05f + tangentDir * t * 0.045f, t, 18.0f, 0.880f) * rimWide * activation;
    float entranceMotes = SparkField(gateMaskUV * 1.20f + radialDir * (0.08f - t * 0.020f) + tangentDir * t * 0.038f, t, 13.0f, 0.800f) * portalMask * activation * (1.0f - smoothstep(0.86f, 0.98f, radius));
    float goldMotes = saturate(depthMotes * 0.75f + rimMotes * 0.55f + entranceMotes * 0.65f);
    float starwardCore = exp(-radius * radius * 5.2f) * activation;
    float astralDust = SparkField(gateMaskUV * 0.92f + tangentDir * t * 0.018f - radialDir * t * 0.012f, t, 24.0f, 0.910f) * deepInterior;
    float distantNebula = Fbm2(gateMaskUV * 4.6f + radialDir * 0.35f + float2(-t * 0.018f, t * 0.012f)) * deepInterior;

    float3 goldDeep = float3(0.520f, 0.360f, 0.120f);
    float3 goldMid = float3(0.980f, 0.720f, 0.280f);
    float3 goldHot = float3(1.000f, 0.930f, 0.620f);
    float3 coreDeep = float3(0.040f, 0.125f, 0.190f);
    float3 coreMid = float3(0.470f, 0.760f, 0.900f);
    float3 coreHot = float3(0.950f, 0.985f, 0.930f);
    float3 violetDeep = float3(0.32f, 0.20f, 0.92f);
    float3 violetMid = float3(0.75f, 0.45f, 1.00f);
    if (mode > 1.5f)
    {
        goldDeep = float3(0.430f, 0.285f, 0.100f);
        goldMid = float3(0.880f, 0.610f, 0.220f);
        goldHot = float3(1.000f, 0.875f, 0.520f);
        coreDeep = float3(0.030f, 0.095f, 0.155f);
        coreMid = float3(0.380f, 0.680f, 0.845f);
        coreHot = float3(0.910f, 0.975f, 0.980f);
    }

    float warmBridge = saturate(
        rimInner * 0.48f +
        tunnelRings * 0.42f +
        broadArms * smoothstep(0.36f, 0.82f, radius) * 0.28f +
        petalRibbons * 0.34f +
        softGateVeil * 0.16f +
        pullStreak * 0.16f);
    float flow = saturate(broadArms * 0.42f + counterArms * 0.30f + midNoise * 0.22f + bakedLine * 0.10f);
    float2 distortVector = tangentDir * (broadArms * 0.60f + fineFilaments * 0.35f - 0.22f);
    distortVector += radialDir * (coarse - 0.5f + innerVoid * 0.30f);
    float distortStrength = portalMask * (0.002f + flow * 0.013f + rimWide * 0.022f + innerVoid * 0.010f) * intensity * lerp(0.25f, 1.0f, activation);
    float2 distortUV = screenUV + distortVector * distortStrength;
    float3 scene = grabTex.SampleLevel(smp, saturate(distortUV), 0).rgb;

    float coreEnergy = saturate(counterArms * 0.25f + fineFilaments * 0.22f + innerBand * 0.30f + sparkInside * 0.38f + electricWeb * 0.48f + corePulse * 0.20f + petalRibbons * 0.42f + pearlyBloom * 0.26f + goldMotes * 0.44f + astralDust * 0.52f + starwardCore * 0.18f);
    float3 innerColor = lerp(coreDeep, coreMid, saturate(flow + innerBand * 0.40f));
    innerColor = lerp(innerColor, coreHot, saturate(coreEnergy * 0.58f + glintB * 0.26f + electricWeb * 0.34f));
    innerColor = lerp(innerColor, float3(0.070f, 0.018f, 0.004f), depthCore * 0.42f);
    float3 bridgeColor = lerp(coreHot, goldHot, 0.24f);
    float3 pearlColor = float3(0.930f, 0.990f, 1.000f);
    float3 veilColor = float3(0.620f, 0.850f, 0.960f);
    float3 spaceColor = lerp(float3(0.030f, 0.110f, 0.180f), float3(0.300f, 0.620f, 0.820f), saturate(spaceNebula * 0.48f + distantNebula * 0.28f + corePulse * 0.10f));
    spaceColor = lerp(spaceColor, float3(0.012f, 0.035f, 0.070f), eventHorizon * 0.24f);
    spaceColor = lerp(spaceColor, float3(0.760f, 0.915f, 1.000f), saturate(distantNebula * 0.22f + tunnelRings * 0.14f));
    spaceColor += float3(1.000f, 0.930f, 0.610f) * starwardCore * 0.12f;
    spaceColor = lerp(spaceColor, bridgeColor, saturate(tunnelRings * 0.40f + rimInner * 0.18f));
    spaceColor += coreHot * (tunnelRings * 0.14f + eventHorizon * 0.05f);
    innerColor = lerp(innerColor, spaceColor, saturate(spaceMask * 0.42f + deepInterior * 0.42f));
    innerColor = lerp(innerColor, pearlColor, saturate(softGateVeil * 0.14f + pearlyBloom * 0.18f));
    innerColor = lerp(innerColor, veilColor, saturate(deepInterior * 0.18f + softGateVeil * 0.08f + pearlyBloom * 0.08f));
    innerColor = lerp(innerColor, bridgeColor, warmBridge * 0.18f);
    innerColor += lerp(coreHot, goldHot, 0.26f) * distantStars * 0.52f;
    innerColor += lerp(coreHot, goldHot, 0.42f) * goldMotes * 0.36f;
    innerColor += float3(0.880f, 0.970f, 1.000f) * astralDust * 0.42f;
    innerColor += float3(1.000f, 0.945f, 0.640f) * starwardCore * 0.18f;
    innerColor += pearlColor * petalRibbons * 0.28f;

    float rimEnergy = saturate(rimWide * 0.24f + rimHot * 0.85f + outerRunes * 0.64f + sparkRim * 0.34f + glintA * 0.55f);
    float3 rimColor = lerp(goldDeep, goldMid, saturate(rimHot * 0.85f + outerRunes * 0.45f + rimWide * 0.26f));
    rimColor = lerp(rimColor, goldHot, saturate(rimEnergy));

    float sealBand = 0.0f;
    if (mode > 1.5f)
    {
        float radialStripe = RingMask(frac(angle * 1.9f + t * 0.2f), 0.5f, 0.05f);
        sealBand = radialStripe * RingMask(radius, 0.54f, 0.07f) * portalMask;
        innerColor += float3(0.72f, 0.78f, 1.0f) * sealBand * intensity;
    }

    float atmosphericMist = (1.0f - coreHole) * outerClip * (0.16f + coarse * 0.13f);
    float halo = pow(saturate(1.0f - abs(radius - outerRadius) / 0.32f), 2.0f) * outerClip;
    float colorBlend = saturate(rimWide * 0.20f + rimHot * 1.05f + outerRunes * 0.70f);
    float3 dormantColor = float3(0.035f, 0.070f, 0.105f);
    float3 portalColor = lerp(innerColor, rimColor, colorBlend);
    portalColor = lerp(portalColor, portalColor * float3(0.68f, 0.76f, 0.86f), shellDepth * 0.22f);
    portalColor = lerp(portalColor, portalColor * float3(0.42f, 0.56f, 0.72f), innerDepthShade * 0.26f);
    portalColor = lerp(portalColor, coreDeep * 0.62f, throatShade * 0.18f);
    portalColor = lerp(dormantColor, portalColor, lerp(0.18f, 1.0f, activation));
    float luminousCurtain = saturate(softGateVeil * 0.50f + pearlyBloom * 0.38f + petalRibbons * 0.20f + deepInterior * 0.82f + tunnelRings * 0.34f + goldMotes * 0.32f + astralDust * 0.40f + spaceMask * 0.68f + starwardCore * 0.18f);

    float alpha = atmosphericMist * 0.22f;
    alpha += (broadArms * 0.07f + counterArms * 0.07f + fineFilaments * 0.08f + innerBand * 0.10f + softGateVeil * 0.10f + pearlyBloom * 0.10f + petalRibbons * 0.12f) * portalMask;
    alpha += rimWide * 0.14f + rimHot * 0.42f + outerRunes * 0.24f + halo * 0.10f;
    alpha += sparkInside * 0.20f + sparkRim * 0.26f + glintA * 0.13f + glintB * 0.11f + sealBand * 0.22f;
    alpha += electricWeb * 0.16f + corePulse * 0.07f + distantStars * 0.18f + goldMotes * 0.32f + astralDust * 0.36f;
    alpha += pullSpark * 0.30f + pullStreak * 0.10f + shockRing * 0.42f + shockHalo * 0.15f;
    alpha += spaceMask * 0.08f + deepInterior * 0.18f + eventHorizon * 0.05f + tunnelRings * 0.20f + tunnelShaft * 0.07f;
    alpha += luminousCurtain * 0.50f;
    alpha += shellDepth * 0.12f + innerDepthShade * 0.10f + entrySeal * 0.18f + throatShade * 0.16f;
    alpha = saturate(alpha * (0.86f + intensity * 0.20f) * outerClip * color.a * lerp(0.18f, 1.0f, activation));

    float3 tint = lerp(float3(1.0f, 1.0f, 1.0f), saturate(color.rgb + 0.16f), 0.12f);
    float emission = saturate(rimEnergy * 0.72f + coreEnergy * 0.38f + fineFilaments * 0.18f + glintA * 0.55f + glintB * 0.32f + softGateVeil * 0.18f + pearlyBloom * 0.16f + goldMotes * 0.36f + astralDust * 0.42f + starwardCore * 0.20f + sealBand * 0.42f);
    float interiorOcclusion = saturate(deepInterior * 0.78f + spaceMask * 0.58f + tunnelRings * 0.40f + eventHorizon * 0.20f + luminousCurtain * 1.18f + shellDepth * 0.42f + innerDepthShade * 0.52f + throatShade * 0.34f + entrySeal * 0.34f);
    float3 refractedScene = scene * lerp(0.42f + atmosphericMist * 0.05f, 0.015f, interiorOcclusion);
    float portalBlend = saturate(alpha + emission * 0.46f + deepInterior * 0.78f + tunnelRings * 0.32f + eventHorizon * 0.16f + warmBridge * 0.12f + softGateVeil * 0.26f + luminousCurtain * 1.18f + shellDepth * 0.20f + innerDepthShade * 0.22f + throatShade * 0.34f + entrySeal * 0.46f);
    float3 finalColor = lerp(refractedScene, portalColor * tint, portalBlend);
    finalColor += rimColor * (rimHot * 0.24f + outerRunes * 0.20f + sparkRim * 0.10f + glintA * 0.30f) * intensity;
    finalColor += lerp(coreHot, goldHot, 0.38f) * (fineFilaments * 0.06f + sparkInside * 0.11f + glintB * 0.16f + electricWeb * 0.24f + corePulse * 0.10f + petalRibbons * 0.12f) * intensity;
    finalColor += bridgeColor * (tunnelRings * 0.20f + eventHorizon * 0.05f + warmBridge * 0.14f + pearlyBloom * 0.10f) * intensity;
    finalColor += goldHot * goldMotes * (0.28f + intensity * 0.18f);
    finalColor += float3(0.780f, 0.940f, 1.000f) * astralDust * (0.18f + intensity * 0.10f);
    finalColor += float3(1.000f, 0.940f, 0.650f) * starwardCore * 0.12f * intensity;
    finalColor += goldHot * (pullSpark * 0.26f + pullStreak * 0.16f + shockRing * 0.34f + shockHalo * 0.18f) * intensity;
    finalColor += coreHot * entrySeal * 0.20f * intensity;

    if (alpha < 0.01f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}
