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

    uint depthWidth = 1;
    uint depthHeight = 1;
    depthTex.GetDimensions(depthWidth, depthHeight);
    uint2 depthCoord = min(
        uint2(screenUV * float2(depthWidth, depthHeight)),
        uint2(depthWidth - 1, depthHeight - 1));
    float rawSceneDepth = depthTex.Load(int3(depthCoord, 0));
    float rawPortalDepth = saturate(input.screenPos.z / input.screenPos.w);

    // Special materials are rendered without a DSV, so reject fragments hidden by opaque scene depth explicitly.
    clip(rawSceneDepth - rawPortalDepth + 0.00001f);

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

    if (mode > 2.5f)
    {
        // Fade a barrier only while the camera is crossing its surface.
        // This prevents a nearby wall from behaving like a full-screen overlay.
        float3 barrierNormal = normalize(input.normal);
        float cameraPlaneDistance = abs(dot(cameraWorldPosition - input.worldPos, barrierNormal));
        float cameraSurfaceFade = smoothstep(1.25f, 4.50f, cameraPlaneDistance);

        float2 absP = abs(p);
        float edgeDistance = 1.0f - max(absP.x, absP.y);
        float outerMask = smoothstep(0.0f, 0.055f, edgeDistance);
        float border = 1.0f - smoothstep(0.018f, 0.115f, edgeDistance);

        float revealTop = -1.04f + activation * 2.12f;
        float revealMask = 1.0f - smoothstep(revealTop - 0.045f, revealTop + 0.075f, p.y);
        revealMask *= outerMask;

        float2 barrierUV = p * float2(8.0f + detail * 0.11f, 4.2f + detail * 0.045f);
        float2 barrierCell = frac(barrierUV + float2(t * 0.08f, -t * 0.13f)) - 0.5f;
        float diamondDistance = abs(abs(barrierCell.x) + abs(barrierCell.y) - 0.43f);
        float diamondLattice = 1.0f - smoothstep(0.025f, 0.085f, diamondDistance);

        float verticalFilament = pow(
            saturate(0.5f + 0.5f * sin((p.x * (13.0f + detail * 0.12f) + t * 0.42f) * 6.2831853f)),
            13.0f);
        float counterFilament = pow(
            saturate(0.5f + 0.5f * sin((p.x * 7.0f - p.y * 2.6f - t * 0.31f) * 6.2831853f)),
            18.0f);
        float scanBand = pow(
            saturate(0.5f + 0.5f * sin((p.y * 3.2f - t * 0.72f) * 6.2831853f)),
            8.0f);

        float shardNoise = Fbm2(barrierUV * 0.54f + float2(t * 0.19f, -t * 0.34f));
        float shardGlint = pow(saturate(shardNoise * 1.32f - 0.72f), 3.2f);
        float energy = saturate(
            diamondLattice * 0.58f +
            verticalFilament * 0.82f +
            counterFilament * 0.54f +
            scanBand * 0.30f +
            shardGlint * 0.72f +
            border * 1.15f);

        float burstLine = 1.0f - smoothstep(0.035f, 0.18f, abs(p.y - revealTop));
        burstLine *= activationBurst * outerMask;
        energy = saturate(energy + burstLine * 1.35f);

        float2 distortDirection = float2(
            sin(p.y * 12.0f + t * 1.7f),
            cos(p.x * 9.0f - t * 1.3f));
        float distortStrength = (0.0018f + energy * 0.0065f) * activation;
        float3 scene = grabTex.SampleLevel(smp, saturate(screenUV + distortDirection * distortStrength), 0).rgb;

        float3 deepColor = float3(0.015f, 0.055f, 0.145f);
        float3 cyanColor = float3(0.055f, 0.760f, 1.000f);
        float3 magentaColor = float3(0.900f, 0.160f, 1.000f);
        float chroma = 0.5f + 0.5f * sin(p.x * 8.2f - p.y * 5.6f + t * 1.9f + shardNoise * 4.0f);
        float3 barrierColor = lerp(cyanColor, magentaColor, chroma * 0.42f);
        barrierColor = lerp(deepColor, barrierColor, 0.32f + energy * 0.68f);
        barrierColor = lerp(barrierColor, float3(0.82f, 0.98f, 1.0f), saturate(border + burstLine));
        barrierColor *= saturate(color.rgb + 0.34f);

        float veil = 0.15f + diamondLattice * 0.08f + scanBand * 0.055f;
        float alpha = saturate((veil + energy * 0.48f + border * 0.26f) * revealMask * color.a);
        alpha *= cameraSurfaceFade;
        float3 refractedScene = scene * lerp(0.88f, 0.30f, alpha);
        float3 finalBarrierColor = lerp(refractedScene, barrierColor * (0.70f + intensity * 0.38f), alpha);
        finalBarrierColor += barrierColor * energy * (0.22f + intensity * 0.16f) * revealMask;

        if (alpha < 0.008f)
        {
            discard;
        }
        return float4(finalBarrierColor, alpha);
    }

    // Circular portals: three readable ribbons, a quiet throat and a luminous rim.
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float outerRadius = 0.74f;
    float edgeAA = max(fwidth(radius), 0.002f);
    float mask = 1.0f - smoothstep(outerRadius - edgeAA, outerRadius + edgeAA, radius);
    clip(mask - 0.001f);
    float r = radius / outerRadius;
    bool cleared = mode > 1.5f;
    float3 deepColor = cleared ? float3(0.015f, 0.040f, 0.12f) : float3(0.095f, 0.027f, 0.078f);
    float3 midColor = cleared ? float3(0.035f, 0.22f, 0.35f) : float3(0.32f, 0.13f, 0.075f);
    float3 ribbonColor = cleared ? float3(0.12f, 0.58f, 0.72f) : float3(0.90f, 0.43f, 0.095f);
    float3 hotColor = cleared ? float3(0.65f, 0.96f, 1.0f) : float3(1.0f, 0.85f, 0.46f);
    float depth = smoothstep(0.12f, 0.98f, r);
    float3 portalColor = lerp(deepColor, midColor, depth);
    float ribbon = 0.0f;
    float fineEdge = 0.0f;
    float veils = 0.0f;
    float pixelWidth = max(length(ddx(p)), length(ddy(p))) / outerRadius;
    float aa = max(pixelWidth, 0.002f);
    float curl = 3.4f + min(max(effectScale, 0.0f), 2.0f) * 0.75f;
    // Different lengths and widths avoid the silhouette of a rigid three-bladed fan.
    [unroll] for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float tip = 0.16f + fi * 0.055f;
        float phase = fi * 2.094395f + 0.16f * sin(t * 0.21f + fi * 2.1f);
        phase += (1.0f - r) * curl - t * 0.27f;
        float delta = atan2(sin(angle - phase), cos(angle - phase));
        float distanceToRibbon = abs(delta) * max(r, 0.08f);
        float taper = smoothstep(tip, tip + 0.32f, r);
        float width = (0.013f + 0.035f * r) * (1.0f + fi * 0.17f) * taper;
        float envelope = smoothstep(tip, tip + 0.13f, r) * (1.0f - smoothstep(0.91f, 1.0f, r));
        ribbon += (1.0f - smoothstep(width, width + aa * 1.5f, distanceToRibbon)) * envelope;
        fineEdge += (1.0f - smoothstep(width * 0.18f, width * 0.18f + aa, distanceToRibbon)) * envelope;
        // A broad dim veil behind each thin stream gives depth without high-frequency noise.
        veils += exp(-distanceToRibbon * distanceToRibbon / max(width * width * 24.0f, 0.0001f)) * envelope;
    }
    portalColor = lerp(portalColor, midColor * 1.15f, saturate(veils * 0.24f));
    portalColor = lerp(portalColor, ribbonColor, saturate(ribbon * (0.48f + depth * 0.30f)));
    portalColor += hotColor * saturate(fineEdge) * 0.34f;
    // Recessed partial arcs move more slowly than the foreground streams.
    float innerRim = 0.0f;
    [unroll] for (int j = 0; j < 2; ++j) {
        float fj = float(j);
        float arcRadius = 0.30f + fj * 0.17f + 0.012f * sin(angle * 2.0f + t * 0.11f);
        float arc = 1.0f - smoothstep(0.002f, 0.002f + aa, abs(r - arcRadius));
        arc *= smoothstep(0.35f, 0.85f, sin(angle * 2.0f + fj * 2.6f + t * 0.13f));
        innerRim += arc;
    }
    portalColor += ribbonColor * innerRim * 0.055f;
    float rim = 1.0f - smoothstep(0.004f, 0.009f + aa, abs(r - 0.97f));
    float rimHalo = exp(-pow((r - 0.97f) / (0.022f + softness * 0.018f), 2.0f));
    float glints = 0.0f;
    [unroll] for (int k = 0; k < 2; ++k) {
        float glintAngle = 0.85f + float(k) * 3.141593f + t * 0.16f;
        float2 glintPos = float2(cos(glintAngle), sin(glintAngle)) * 0.97f;
        float d = length(p / outerRadius - glintPos);
        glints += 1.0f - smoothstep(0.013f, 0.036f + aa, d);
    }
    float breath = 0.96f + 0.04f * sin(t * 0.8f);
    float brightness = min(intensity, 3.5f);
    portalColor += hotColor * (rim * (0.65f + brightness * 0.25f)
        + rimHalo * 0.12f + glints * 0.75f) * breath;
    // Four analytic motes travel inward and fade before their radial wrap.
    [unroll] for (int m = 0; m < 4; ++m) {
        float fm = float(m);
        float progress = frac(t * 0.045f + fm * 0.25f);
        float moteRadius = lerp(0.82f, 0.18f, progress);
        float moteAngle = fm * 2.399963f + (1.0f - moteRadius) * curl - t * 0.27f;
        float2 motePos = float2(cos(moteAngle), sin(moteAngle)) * moteRadius;
        float d = length(p / outerRadius - motePos);
        float fade = smoothstep(0.0f, 0.12f, progress) * (1.0f - smoothstep(0.84f, 1.0f, progress));
        float mote = 1.0f - smoothstep(0.005f, 0.012f + aa, d);
        portalColor += hotColor * mote * fade * 0.75f;
    }
    portalColor += hotColor * activationBurst * (rim * 0.55f + saturate(ribbon) * 0.10f);
    float3 tint = lerp(float3(1, 1, 1), saturate(color.rgb + 0.16f), 0.12f);
    portalColor *= tint;
    float3 dormant = lerp(deepColor * 0.7f, midColor * 0.45f, rim);
    portalColor = lerp(dormant, portalColor, activation);
    // Standard alpha blending composites once; no precomposited scene color here.
    float alpha = mask * saturate(color.a) * lerp(0.18f, 0.98f, activation);
    return float4(portalColor, alpha);
}
