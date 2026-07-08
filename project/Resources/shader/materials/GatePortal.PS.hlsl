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

    float swirlPhaseA = angle + radius * (4.10f + effectScale * 1.20f) - t * 0.35f;
    float swirlPhaseB = -angle + radius * (5.60f + effectScale * 1.10f) + t * 0.18f;
    float2 flowUV =
        p * (2.10f + effectScale * 0.40f) +
        float2(cos(swirlPhaseA), sin(swirlPhaseA)) * (radius * (2.40f + detail * 0.020f) + 0.15f) +
        float2(cos(swirlPhaseB), sin(swirlPhaseB)) * (radius * (1.30f + detail * 0.012f) + 0.07f);
    float coarse = Fbm2(flowUV * (1.00f + detail * 0.060f));
    float midNoise = Fbm2(flowUV * (2.45f + detail * 0.130f) + float2(t * 0.27f, -t * 0.19f));
    float fine = Fbm2(flowUV * (5.10f + detail * 0.160f) + float2(-t * 0.42f, t * 0.31f));

    float bakedLine = 0.0f;
    float armCount = 5.0f + floor(detail * 0.055f);
    float counterArmCount = floor(armCount * 0.65f + 2.5f);
    float filamentArmCount = floor(armCount * 1.85f + 1.5f);
    float broadArms = ArcMask(angle, radius, armCount, -t * 2.20f + coarse * 3.5f, 2.85f) * portalMask;
    float counterArms = ArcMask(angle, radius, counterArmCount, t * 1.44f + midNoise * 3.9f, 4.20f) * portalMask;
    float fineFilaments = ArcMask(angle, radius, filamentArmCount, -t * 3.10f + fine * 5.0f, 13.5f) * portalMask;
    fineFilaments = saturate(fineFilaments * (0.24f + midNoise * 0.38f) + bakedLine * 0.025f);
    float softGateVeil = (1.0f - smoothstep(0.18f, 0.90f, radius)) * portalMask * (0.38f + coarse * 0.34f);
    float pearlyBloom = exp(-radius * radius * 2.25f) * portalMask * activation * (0.42f + 0.38f * midNoise);
    float petalRibbons =
        ArcMask(angle, radius, 6.0f, -t * 1.12f + coarse * 2.1f, 3.4f) *
        smoothstep(0.16f, 0.34f, radius) *
        (1.0f - smoothstep(0.68f, 0.88f, radius)) *
        portalMask;

    float innerBand = RingMask(radius, 0.485f + sin(t * 0.77f + coarse) * 0.018f, 0.055f) * portalMask;
    float outerRunes = 0.0f;
    float sparkInside = SparkField(gateMaskUV + radialDir * 0.060f + tangentDir * t * 0.018f, t, 9.0f, 0.83f) * portalMask * activation;
    float sparkRim = SparkField(gateMaskUV * 1.4f + float2(t * 0.025f, -t * 0.015f), t, 13.0f, 0.89f) * rimWide * activation;
    float pullZone = smoothstep(outerRadius - 0.04f, outerRadius + 0.10f, radius) * (1.0f - smoothstep(1.17f, 1.34f, radius)) * quadSafeFade;
    float2 pullUV = gateMaskUV * 1.75f + radialDir * (t * 0.115f) + tangentDir * (t * 0.055f);
    float pullSpark = SparkField(pullUV, t, 12.0f, 0.865f) * pullZone * activation;
    float pullStreak = pow(saturate(ArcMask(angle, radius, 10.0f, -t * 3.2f + coarse * 2.4f, 9.0f)), 1.2f) * pullZone * activation;
    float glintA = exp(-dot(p - float2(-0.22f, 0.31f), p - float2(-0.22f, 0.31f)) * 58.0f) * outerClip * 0.18f;
    float glintB = exp(-dot(p - float2(0.32f, -0.18f), p - float2(0.32f, -0.18f)) * 70.0f) * portalMask * 0.12f;
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
    float paintRadius = saturate(radius + (coarse - 0.5f) * 0.055f + sin(angle * 3.0f + radius * 8.0f - t * 0.70f) * 0.020f);
    float portalWarmth = pow(saturate(1.0f - paintRadius * 0.98f), 0.92f) * portalMask * activation;
    float centerHeat = pow(saturate(1.0f - paintRadius * 1.10f), 1.00f) * outerClip * activation;
    float centerEmber = pow(saturate(1.0f - paintRadius * 1.90f), 1.55f) * outerClip * activation;
    centerEmber *= 0.72f + 0.28f * sin(t * 3.6f + coarse * 4.4f);
    float centerDepth = pow(saturate(1.0f - paintRadius * 2.35f), 0.92f) * outerClip * activation;
    float centerThroat = pow(saturate(1.0f - paintRadius * 4.20f), 1.35f) * outerClip * activation;
    float centerAnnulus = RingMask(paintRadius, 0.33f + sin(t * 0.8f) * 0.012f, 0.28f) * portalMask * activation;
    float paintTwist = angle * 3.0f + radius * (5.10f + coarse * 1.25f) - t * 0.74f;
    float paintCounter = -angle * 2.0f + radius * (7.30f + midNoise * 1.80f) + t * 0.36f;
    float paintWaveA = 0.5f + 0.5f * sin(paintTwist + coarse * 4.20f);
    float paintWaveB = 0.5f + 0.5f * sin(paintCounter + fine * 5.80f);
    float radialPaintFade = smoothstep(0.12f, 0.25f, paintRadius) * (1.0f - smoothstep(0.55f, 0.76f, paintRadius)) * portalMask * activation;
    float innerPaintFade = (1.0f - smoothstep(0.50f, 0.72f, paintRadius)) * portalMask * activation;
    float creamRibbon = pow(saturate(paintWaveA), 1.45f) * radialPaintFade * (0.24f + 0.46f * midNoise);
    float orangeRibbon = pow(saturate(1.0f - abs(paintWaveA - 0.48f) * 2.0f), 1.20f) * radialPaintFade * 0.82f;
    float redRibbon = pow(saturate(paintWaveB), 2.30f) * innerPaintFade * smoothstep(0.08f, 0.50f, paintRadius);
    float shadowRibbon = pow(saturate(1.0f - paintWaveA), 2.05f) * pow(saturate(paintWaveB), 0.75f) * innerPaintFade * smoothstep(0.08f, 0.58f, paintRadius);
    float darkCurl = pow(saturate(1.0f - abs(paintWaveB - 0.22f) * 3.2f), 1.15f) * innerPaintFade * smoothstep(0.06f, 0.52f, paintRadius);
    float oliveBruise = pow(saturate(1.0f - paintWaveB), 2.60f) * shadowRibbon * smoothstep(0.18f, 0.60f, paintRadius) * 0.65f;
    float spiralSink = saturate(pow(saturate(1.0f - paintRadius * (2.90f + shadowRibbon * 0.85f)), 1.30f) + darkCurl * 0.45f) * portalMask * activation;

    float warmSpiral = saturate(
        broadArms * 0.66f +
        counterArms * 0.44f +
        fineFilaments * 0.28f +
        petalRibbons * 0.42f +
        bakedLine * portalMask * 0.08f);
    float stripeMask = smoothstep(0.12f, 0.24f, paintRadius) * (1.0f - smoothstep(0.62f, 0.84f, paintRadius)) * portalMask;
    float orangeStripe = pow(saturate(0.50f + 0.50f * sin(angle * 5.0f + radius * 13.2f - t * 2.05f + coarse * 2.8f)), 2.25f) * stripeMask;
    float yellowStripe = pow(saturate(0.50f + 0.50f * sin(angle * -4.0f + radius * 10.4f + t * 1.55f + midNoise * 2.4f)), 3.20f) * stripeMask;
    warmSpiral = saturate(warmSpiral + orangeStripe * 0.30f + yellowStripe * 0.22f);

    float3 goldDeep = float3(0.470f, 0.210f, 0.045f);
    float3 goldMid = float3(1.000f, 0.545f, 0.090f);
    float3 goldHot = float3(1.000f, 0.910f, 0.430f);
    float3 coreDeep = float3(0.155f, 0.045f, 0.010f);
    float3 coreMid = float3(0.960f, 0.360f, 0.040f);
    float3 coreHot = float3(1.000f, 0.880f, 0.500f);
    float3 furnaceOrange = float3(1.000f, 0.430f, 0.055f);
    float3 centerOrange = float3(0.600f, 0.115f, 0.012f);
    float3 centerAmber = float3(1.000f, 0.470f, 0.065f);
    float3 throatColor = float3(0.145f, 0.026f, 0.004f);
    float3 paintCreamColor = float3(1.000f, 0.760f, 0.295f);
    float3 paintTangerineColor = float3(1.000f, 0.315f, 0.025f);
    float3 paintRedBrownColor = float3(0.380f, 0.050f, 0.010f);
    float3 paintDeepBrownColor = float3(0.105f, 0.020f, 0.006f);
    float3 paintOliveShadowColor = float3(0.185f, 0.130f, 0.035f);
    float3 pearlColor = float3(1.000f, 0.760f, 0.285f);
    float3 veilColor = float3(1.000f, 0.405f, 0.075f);
    float3 spaceDeepColor = float3(0.145f, 0.040f, 0.010f);
    float3 spaceMidColor = float3(0.940f, 0.315f, 0.045f);
    float3 spaceDarkColor = float3(0.060f, 0.014f, 0.004f);
    float3 spaceHotColor = float3(1.000f, 0.765f, 0.330f);
    float3 moteColor = float3(1.000f, 0.620f, 0.180f);
    float3 starColor = float3(1.000f, 0.850f, 0.360f);
    float3 orangeStripeColor = float3(1.000f, 0.470f, 0.090f);
    float3 yellowStripeColor = float3(1.000f, 0.900f, 0.360f);
    if (mode > 1.5f)
    {
        goldDeep = float3(0.030f, 0.185f, 0.285f);
        goldMid = float3(0.070f, 0.610f, 0.900f);
        goldHot = float3(0.650f, 0.965f, 1.000f);
        coreDeep = float3(0.006f, 0.040f, 0.100f);
        coreMid = float3(0.020f, 0.370f, 0.650f);
        coreHot = float3(0.420f, 0.910f, 1.000f);
        furnaceOrange = float3(0.060f, 0.720f, 1.000f);
        centerOrange = float3(0.010f, 0.165f, 0.335f);
        centerAmber = float3(0.210f, 0.910f, 1.000f);
        throatColor = float3(0.002f, 0.022f, 0.070f);
        paintCreamColor = float3(0.610f, 0.960f, 1.000f);
        paintTangerineColor = float3(0.020f, 0.600f, 0.960f);
        paintRedBrownColor = float3(0.006f, 0.160f, 0.360f);
        paintDeepBrownColor = float3(0.001f, 0.030f, 0.115f);
        paintOliveShadowColor = float3(0.030f, 0.180f, 0.260f);
        pearlColor = float3(0.700f, 0.970f, 1.000f);
        veilColor = float3(0.030f, 0.620f, 0.980f);
        spaceDeepColor = float3(0.002f, 0.040f, 0.100f);
        spaceMidColor = float3(0.020f, 0.330f, 0.720f);
        spaceDarkColor = float3(0.001f, 0.014f, 0.045f);
        spaceHotColor = float3(0.420f, 0.920f, 1.000f);
        moteColor = float3(0.160f, 0.800f, 1.000f);
        starColor = float3(0.650f, 0.960f, 1.000f);
        orangeStripeColor = float3(0.030f, 0.520f, 0.950f);
        yellowStripeColor = float3(0.560f, 0.960f, 1.000f);
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
    innerColor = lerp(innerColor, coreHot, saturate(coreEnergy * 0.44f + glintB * 0.08f + electricWeb * 0.26f));
    innerColor = lerp(innerColor, float3(0.070f, 0.018f, 0.004f), depthCore * 0.42f);
    innerColor = lerp(innerColor, paintCreamColor, saturate(creamRibbon * 0.24f));
    innerColor = lerp(innerColor, furnaceOrange, saturate(portalWarmth * 0.26f + orangeRibbon * 0.28f + centerAnnulus * 0.10f));
    innerColor = lerp(innerColor, paintTangerineColor, saturate(orangeRibbon * 0.34f + redRibbon * 0.14f));
    innerColor = lerp(innerColor, paintRedBrownColor, saturate(redRibbon * 0.60f + shadowRibbon * 0.42f + darkCurl * 0.30f + centerDepth * 0.28f));
    innerColor = lerp(innerColor, paintDeepBrownColor, saturate(darkCurl * 0.58f + spiralSink * 0.32f));
    innerColor = lerp(innerColor, paintOliveShadowColor, saturate(oliveBruise * 0.22f));
    innerColor = lerp(innerColor, throatColor, saturate(centerThroat * 0.84f + spiralSink * 0.50f));
    innerColor += paintCreamColor * (creamRibbon * 0.10f + centerAnnulus * 0.10f);
    innerColor += centerAmber * (centerEmber * 0.06f + orangeRibbon * 0.05f);
    float3 bridgeColor = lerp(coreHot, goldHot, 0.36f);
    float3 spaceColor = lerp(spaceDeepColor, spaceMidColor, saturate(spaceNebula * 0.48f + distantNebula * 0.28f + corePulse * 0.10f));
    spaceColor = lerp(spaceColor, spaceDarkColor, eventHorizon * 0.26f);
    spaceColor = lerp(spaceColor, spaceHotColor, saturate(distantNebula * 0.22f + tunnelRings * 0.14f + yellowStripe * 0.16f));
    spaceColor += starColor * starwardCore * 0.12f;
    spaceColor = lerp(spaceColor, bridgeColor, saturate(tunnelRings * 0.40f + rimInner * 0.18f));
    spaceColor += coreHot * (tunnelRings * 0.14f + eventHorizon * 0.05f);
    innerColor = lerp(innerColor, spaceColor, saturate(spaceMask * 0.42f + deepInterior * 0.42f));
    innerColor = lerp(innerColor, pearlColor, saturate(softGateVeil * 0.14f + pearlyBloom * 0.18f));
    innerColor = lerp(innerColor, veilColor, saturate(deepInterior * 0.12f + softGateVeil * 0.08f + pearlyBloom * 0.08f + orangeStripe * 0.18f));
    innerColor = lerp(innerColor, bridgeColor, saturate(warmBridge * 0.22f + warmSpiral * 0.24f));
    innerColor += lerp(coreMid, goldHot, 0.38f) * distantStars * 0.42f;
    innerColor += lerp(coreMid, goldHot, 0.52f) * goldMotes * 0.42f;
    innerColor += moteColor * astralDust * 0.34f;
    innerColor += starColor * starwardCore * 0.18f;
    innerColor += pearlColor * petalRibbons * 0.28f;
    innerColor += orangeStripeColor * orangeStripe * 0.20f;
    innerColor += yellowStripeColor * yellowStripe * 0.18f;

    float rimEnergy = saturate(rimWide * 0.24f + rimHot * 0.85f + outerRunes * 0.64f + sparkRim * 0.34f + glintA * 0.12f);
    float3 rimColor = lerp(goldDeep, goldMid, saturate(rimHot * 0.85f + outerRunes * 0.45f + rimWide * 0.26f));
    rimColor = lerp(rimColor, goldHot, saturate(rimEnergy));

    float sealBand = 0.0f;
    if (mode > 1.5f)
    {
        float radialStripe = RingMask(frac(angle * 1.9f + t * 0.2f), 0.5f, 0.05f);
        sealBand = radialStripe * RingMask(radius, 0.54f, 0.07f) * portalMask;
        innerColor += pearlColor * sealBand * intensity;
    }

    float atmosphericMist = (1.0f - coreHole) * outerClip * (0.16f + coarse * 0.13f);
    float halo = pow(saturate(1.0f - abs(radius - outerRadius) / 0.32f), 2.0f) * outerClip;
    float colorBlend = saturate(rimWide * 0.20f + rimHot * 1.05f + outerRunes * 0.70f);
    float3 dormantColor = float3(0.035f, 0.070f, 0.105f);
    float3 portalColor = lerp(innerColor, rimColor, colorBlend);
    portalColor = lerp(portalColor, portalColor * float3(0.68f, 0.76f, 0.86f), shellDepth * 0.22f);
    portalColor = lerp(portalColor, portalColor * float3(0.42f, 0.56f, 0.72f), innerDepthShade * 0.26f);
    portalColor = lerp(portalColor, coreDeep * 0.62f, throatShade * 0.18f);
    portalColor = lerp(portalColor, paintCreamColor, saturate(creamRibbon * 0.18f + centerAnnulus * 0.08f));
    portalColor = lerp(portalColor, furnaceOrange, saturate(portalWarmth * 0.22f + orangeRibbon * 0.22f));
    portalColor = lerp(portalColor, paintTangerineColor, saturate(orangeRibbon * 0.28f + redRibbon * 0.10f));
    portalColor = lerp(portalColor, paintRedBrownColor, saturate(redRibbon * 0.56f + shadowRibbon * 0.46f + darkCurl * 0.28f + centerDepth * 0.34f));
    portalColor = lerp(portalColor, paintDeepBrownColor, saturate(darkCurl * 0.54f + spiralSink * 0.34f));
    portalColor = lerp(portalColor, paintOliveShadowColor, saturate(oliveBruise * 0.20f));
    portalColor = lerp(portalColor, throatColor, saturate(centerThroat * 0.86f + spiralSink * 0.54f));
    portalColor += paintCreamColor * (creamRibbon * 0.08f + centerAnnulus * 0.07f);
    portalColor += centerAmber * (centerEmber * 0.05f + orangeRibbon * 0.05f);
    portalColor = lerp(dormantColor, portalColor, lerp(0.18f, 1.0f, activation));
    float luminousCurtain = saturate(softGateVeil * 0.50f + pearlyBloom * 0.38f + petalRibbons * 0.20f + deepInterior * 0.82f + tunnelRings * 0.34f + goldMotes * 0.32f + astralDust * 0.40f + spaceMask * 0.68f + starwardCore * 0.18f);

    float alpha = atmosphericMist * 0.22f;
    alpha += (broadArms * 0.07f + counterArms * 0.07f + fineFilaments * 0.08f + innerBand * 0.10f + softGateVeil * 0.10f + pearlyBloom * 0.10f + petalRibbons * 0.12f) * portalMask;
    alpha += rimWide * 0.14f + rimHot * 0.42f + outerRunes * 0.24f + halo * 0.10f;
    alpha += sparkInside * 0.20f + sparkRim * 0.26f + glintA * 0.03f + glintB * 0.02f + sealBand * 0.22f;
    alpha += electricWeb * 0.16f + corePulse * 0.07f + distantStars * 0.18f + goldMotes * 0.32f + astralDust * 0.36f;
    alpha += pullSpark * 0.30f + pullStreak * 0.10f + shockRing * 0.42f + shockHalo * 0.15f;
    alpha += spaceMask * 0.08f + deepInterior * 0.18f + eventHorizon * 0.05f + tunnelRings * 0.20f + tunnelShaft * 0.07f;
    alpha += portalWarmth * 0.07f + centerAnnulus * 0.09f + centerEmber * 0.04f + creamRibbon * 0.05f + orangeRibbon * 0.04f;
    alpha += luminousCurtain * 0.50f;
    alpha += shellDepth * 0.12f + innerDepthShade * 0.10f + entrySeal * 0.18f + throatShade * 0.16f;
    alpha = saturate(alpha * (0.86f + intensity * 0.20f) * outerClip * color.a * lerp(0.18f, 1.0f, activation));

    float3 tint = lerp(float3(1.0f, 1.0f, 1.0f), saturate(color.rgb + 0.16f), 0.12f);
    float emission = saturate(rimEnergy * 0.72f + coreEnergy * 0.32f + fineFilaments * 0.12f + glintA * 0.04f + glintB * 0.03f + softGateVeil * 0.10f + pearlyBloom * 0.06f + goldMotes * 0.30f + astralDust * 0.32f + starwardCore * 0.12f + portalWarmth * 0.12f + creamRibbon * 0.12f + orangeRibbon * 0.10f + centerAnnulus * 0.12f + centerEmber * 0.06f + sealBand * 0.42f);
    float interiorOcclusion = saturate(deepInterior * 0.78f + spaceMask * 0.58f + tunnelRings * 0.40f + eventHorizon * 0.20f + luminousCurtain * 1.18f + shellDepth * 0.42f + innerDepthShade * 0.52f + throatShade * 0.34f + entrySeal * 0.34f);
    float3 refractedScene = scene * lerp(0.42f + atmosphericMist * 0.05f, 0.015f, interiorOcclusion);
    float portalBlend = saturate(alpha + emission * 0.40f + deepInterior * 0.78f + tunnelRings * 0.32f + eventHorizon * 0.16f + warmBridge * 0.12f + softGateVeil * 0.16f + luminousCurtain * 0.90f + shellDepth * 0.20f + innerDepthShade * 0.22f + throatShade * 0.34f + entrySeal * 0.46f + portalWarmth * 0.10f + creamRibbon * 0.08f + orangeRibbon * 0.08f + centerAnnulus * 0.10f + centerDepth * 0.08f);
    float3 finalColor = lerp(refractedScene, portalColor * tint, portalBlend);
    finalColor += rimColor * (rimHot * 0.24f + outerRunes * 0.20f + sparkRim * 0.10f + glintA * 0.08f) * intensity;
    finalColor += lerp(coreHot, goldHot, 0.38f) * (fineFilaments * 0.05f + sparkInside * 0.10f + glintB * 0.03f + electricWeb * 0.16f + corePulse * 0.08f + petalRibbons * 0.10f + warmSpiral * 0.07f) * intensity;
    finalColor += bridgeColor * (tunnelRings * 0.20f + eventHorizon * 0.05f + warmBridge * 0.14f + pearlyBloom * 0.10f) * intensity;
    finalColor += goldHot * goldMotes * (0.28f + intensity * 0.18f);
    finalColor += moteColor * astralDust * (0.18f + intensity * 0.10f);
    finalColor += starColor * starwardCore * 0.12f * intensity;
    finalColor += orangeStripeColor * orangeStripe * 0.10f * intensity;
    finalColor += yellowStripeColor * yellowStripe * 0.08f * intensity;
    finalColor = lerp(finalColor, paintCreamColor, saturate(creamRibbon * 0.14f + centerAnnulus * 0.08f));
    finalColor = lerp(finalColor, furnaceOrange, saturate(portalWarmth * 0.16f + orangeRibbon * 0.20f));
    finalColor = lerp(finalColor, paintTangerineColor, saturate(orangeRibbon * 0.24f + redRibbon * 0.08f));
    finalColor = lerp(finalColor, paintRedBrownColor, saturate(redRibbon * 0.54f + shadowRibbon * 0.44f + darkCurl * 0.30f + centerDepth * 0.34f));
    finalColor = lerp(finalColor, paintDeepBrownColor, saturate(darkCurl * 0.56f + spiralSink * 0.34f));
    finalColor = lerp(finalColor, paintOliveShadowColor, saturate(oliveBruise * 0.18f));
    finalColor = lerp(finalColor, throatColor, saturate(centerThroat * 0.88f + spiralSink * 0.56f));
    finalColor += paintCreamColor * (creamRibbon * 0.08f + centerAnnulus * 0.10f) * intensity;
    finalColor += centerAmber * (orangeRibbon * 0.05f + centerEmber * 0.05f) * intensity;
    finalColor += goldHot * (pullSpark * 0.26f + pullStreak * 0.16f + shockRing * 0.34f + shockHalo * 0.18f) * intensity;
    finalColor += coreHot * entrySeal * 0.20f * intensity;

    if (alpha < 0.01f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}
