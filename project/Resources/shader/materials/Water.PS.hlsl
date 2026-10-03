#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
Texture2D<float4> bakedWaterFoamTex : register(t2);
Texture2D<float4> bakedWaterFlowTex : register(t3);
Texture2D<float4> authoredWaterSurfaceTex : register(t4);
SamplerState smp : register(s0);

float2 MirrorUV(float2 p) { return abs(frac(p * 0.5f) * 2.0f - 1.0f); }

// Only authored colors need conversion in the legacy workflow.
// Scene captures and this shader's output are HDR radiance; the final RTV is sRGB.
float3 ToLinear(float3 c)
{
    c = max(c, 0.0f);
    return waterLinearWorkflowEnabled > 0.5f ? c : pow(c, 2.2f);
}


float3 SafeUnit(float3 p, float3 fallback)
{
    return dot(p, p) > 0.000001f ? normalize(p) : fallback;
}

float ReadDepth(float2 uv)
{
    uint w, h;
    depthTex.GetDimensions(w, h);
    return depthTex.Load(int3(min(uint2(saturate(uv) * float2(w, h)), uint2(w - 1, h - 1)), 0));
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float4 p = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f),
        waterInverseViewProjection);
    return p.xyz / max(p.w, 0.000001f);
}

// Smooth random patches instead of periodic wave trains or contour thresholds.
float WaterPatchHash(float2 p)
{
    float3 q = frac(float3(p.x, p.y, p.x) * 0.1031f);
    q += dot(q, q.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

// Return value and analytic slope. Quintic interpolation keeps cell joins smooth.
float3 WaterPatch(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float2 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);
    float a = WaterPatchHash(cell);
    float b = WaterPatchHash(cell + float2(1, 0));
    float c = WaterPatchHash(cell + float2(0, 1));
    float d = WaterPatchHash(cell + float2(1, 1));
    float k = a - b - c + d;
    float value = a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y;
    float2 slope = float2(b - a + k * u.y, c - a + k * u.x) * du;
    float footprint = max(length(ddx(p)), length(ddy(p)));
    float filter = exp(-2.0f * footprint * footprint);
    return float3(lerp(0.5f, value, filter), slope * filter);
}

// Bounded, independently phased motion avoids translating the entire ocean.
float2 SeaOrbit(float t, float phase)
{
    float2 flowPhase = float2(uvOffsetX, uvOffsetY) * 2.0f;
    return float2(sin(t * 1.65f + flowPhase.x + phase),
        sin(t * 1.27f + flowPhase.y + phase * 1.73f));
}

float SunlitFacets(float2 p, float t)
{
    // Opposing bounded layers reshape highlights locally instead of scrolling them.
    float first = WaterPatch(p + SeaOrbit(t, 0.7f) * 0.65f).x;
    float2 rotated = float2(p.x * 0.8f - p.y * 0.6f, p.x * 0.6f + p.y * 0.8f);
    float second = WaterPatch(rotated * 1.83f + SeaOrbit(t * 1.17f, 3.2f) * 0.55f + 19.7f).x;
    return first * 0.65f + second * 0.35f;
}

// Each wavelength travels independently. Derivative filtering removes distant shimmer.
float3 RippleSlope(float2 p)
{
    if (effectType >= 1.5f) {
        // Lighting-only slopes: no geometric waves and no dominant stripe direction.
        float scale = max(waveFrequency, 0.1f) / 5.4f;
        float t = time * max(waveSpeed, 0.0f);
        float2 q = p * scale;
        float3 broad = WaterPatch(q * 0.19f + SeaOrbit(t * 0.65f, 0.0f) * 0.65f);
        float3 middle = WaterPatch(q * 0.67f + SeaOrbit(t, 2.1f) * 0.75f + 17.3f);
        float3 small = WaterPatch(q * 2.1f + SeaOrbit(t * 1.35f, 4.3f) * 0.85f - 23.7f);
        float strength = max(topDownDetail, 0.0f);
        // Ripple Strength controls fine ripples, not the entire moving surface.
        // Retain the base flow even when fine detail is disabled.
        float2 slopes = broad.yz * 0.08625f + middle.yz * 0.05175f
            + small.yz * (0.018f * strength);
        return float3(slopes, 0.0004f * strength * strength);
    }
    const float2 directions[6] = {
        float2(0.94f, 0.34f), float2(0.71f, -0.70f), float2(-0.42f, 0.91f),
        float2(0.99f, 0.12f), float2(0.52f, 0.85f), float2(-0.81f, 0.59f)
    };
    const float frequencies[6] = { 0.38f, 0.61f, 0.97f, 1.57f, 2.51f, 4.03f };
    const float slopes[6] = { 0.060f, 0.038f, 0.022f, 0.012f, 0.006f, 0.003f };
    float2 slope = 0.0f;
    float unresolvedVariance = 0.0f;
    float footprint = max(length(ddx(p)), length(ddy(p)));
    float distantDetail = lerp(1.0f, 0.22f, smoothstep(0.25f, 2.0f, footprint));
    float t = time * max(waveSpeed, 0.0f);
    float scale = max(waveFrequency, 0.1f) / 5.4f;
    [unroll] for (int i = 0; i < 6; ++i) {
        float phase = dot(p, directions[i]) * frequencies[i] * scale
            - t * sqrt(9.81f * frequencies[i]) + float(i) * 1.79f;
        // Filter before the Nyquist limit, then preserve removed slope energy as roughness.
        float phaseWidth = fwidth(phase);
        float filter = exp(-2.0f * phaseWidth * phaseWidth) * distantDetail;
        slope += directions[i] * cos(phase) * slopes[i] * filter;
        unresolvedVariance += 0.5f * slopes[i] * slopes[i] * (1.0f - filter * filter);
    }
    // Calm water has no vertex waves, but retains filtered surface reflections.
    float strength = (effectType >= 1.5f ? 0.6f : saturate(waveHeight / 0.18f)) * max(topDownDetail, 0.0f);
    return float3(slope * strength, unresolvedVariance * strength * strength);
}

// A restrained analytic sky is a fallback environment, not a baked water photograph.
float3 SkyRadiance(float3 direction)
{
    float horizon = pow(1.0f - saturate(direction.y), 3.0f);
    float3 sky = lerp(float3(0.035f, 0.13f, 0.31f), float3(0.45f, 0.62f, 0.73f), horizon);
    float2 q = direction.xz / max(direction.y + 0.3f, 0.15f);
    // Broad non-periodic cloud lobes avoid a repeating checker pattern in the reflection.
    float2 cloudA = (q - float2(-0.85f, 0.35f)) * float2(0.75f, 1.4f);
    float2 cloudB = (q - float2(1.2f, -0.6f)) * float2(1.1f, 0.65f);
    float cloud = (exp(-dot(cloudA, cloudA) * 2.0f) * 0.65f
        + exp(-dot(cloudB, cloudB) * 2.0f) * 0.45f) * smoothstep(0.0f, 0.35f, direction.y);
    if (effectType >= 1.5f) {
        // Preserve directional sky contrast instead of mixing into a constant cyan.
        float3 clearSky = lerp(float3(0.10f, 0.32f, 0.62f),
            float3(0.48f, 0.76f, 0.87f), horizon);
        return lerp(clearSky, float3(0.68f, 0.85f, 0.92f), saturate(cloud) * 0.65f);
    }
    // Stylized blue reflections leave white for sparse highlights and contact foam.
    sky = lerp(sky, float3(0.12f, 0.38f, 0.52f), 0.45f);
    return lerp(sky, float3(0.38f, 0.62f, 0.70f), cloud * 0.18f);
}


float4 main(VSOutput input) : SV_TARGET
{
    // Runtime-only diagnostics bypass lighting and refraction. This shows the
    // clock actually consumed by this shader, not an inspector-side value.
    if (waterDebugView > 0.5f && waterDebugView < 1.5f) {
        float phase = frac(time / 17.0f);
        return float4(phase, 1.0f - phase, 0.15f + 0.7f * frac(time / 23.0f), 1.0f);
    }
    float2 uv = saturate(float2(input.screenPos.x, -input.screenPos.y) / input.screenPos.w * 0.5f + 0.5f);
    float surfaceDepth = input.screenPos.z / input.screenPos.w;
    float sceneDepth = ReadDepth(uv);
    clip(sceneDepth - surfaceDepth + 0.000001f);

    float3 view = SafeUnit(cameraWorldPosition - input.worldPos, float3(0, 1, 0));
    float3 swell = WaterSwell(input.worldPos.xz);
    if (effectType >= 1.5f) swell = 0.0f;
    float3 ripples = RippleSlope(input.worldPos.xz);
    float2 slope = swell.yz + ripples.xy;
    float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    if (waterDebugView > 1.5f) {
        return float4(saturate(float3(slope.x * 4.0f + 0.5f, 0.5f, slope.y * 4.0f + 0.5f)), 1.0f);
    }
    float NdotV = saturate(dot(normal, view));
    float fresnel = 0.02037f + 0.97963f * pow(1.0f - NdotV, 5.0f);

    float depthRange = max(effectScaleX, 1.0f);
    bool hasBottom = sceneDepth < 0.999999f;
    float3 bottom = ReconstructWorld(uv, sceneDepth);
    float thickness = hasBottom ? length(bottom - input.worldPos) : depthRange * 8.0f;
    float verticalDepth = hasBottom ? max(input.worldPos.y - bottom.y, 0.0f) : depthRange * 8.0f;

    // Reject refracted samples in front of the surface to avoid shoreline halos.
    float2 refractedUV = saturate(uv + slope * max(effectScale, 0.0f) * 0.018f
        * saturate(thickness / 2.0f));
    float refractedDepth = ReadDepth(refractedUV);
    if (refractedDepth < surfaceDepth || refractedDepth >= 0.999999f) refractedUV = uv;
    float3 transmittedScene = grabTex.Sample(smp, refractedUV).rgb;

    // Beer-Lambert transmission: red is absorbed first; shallow objects remain visible.
    float3 transmission = exp(-float3(3.8f, 1.35f, 0.62f) * thickness / depthRange);
    if (effectType >= 1.5f) {
        // Clear shallows retain the actual seabed; deep water still absorbs light.
        transmission = exp(-float3(2.2f, 0.72f, 0.34f) * thickness / depthRange);
    }
    if (!hasBottom) transmission = 0.0f;
    float depthBlend = 1.0f - exp(-verticalDepth / depthRange);
    float3 shallow = float3(0.018f, 0.34f, 0.28f);
    float3 deep = float3(0.006f, 0.075f, 0.19f);
    // Reuse the original depth/refraction pipeline with a sunlit blue palette.
    if (effectType >= 1.5f) {
        shallow = float3(0.035f, 0.48f, 0.57f);
        deep = float3(0.008f, 0.20f, 0.48f);
    }
    float3 waterBody = lerp(shallow, deep, depthBlend);
    // Keep the inspector tint useful without letting it wash out depth contrast.
    float3 tint = ToLinear(color.rgb);
    waterBody *= lerp(float3(1, 1, 1), max(tint, 0.02f) / max(max(tint.r, tint.g), max(tint.b, 0.02f)), 0.18f);
    float3 body = transmittedScene * transmission + waterBody * (1.0f - transmission);

    if (effectType >= 1.5f) {
        // View-independent surface modulation keeps motion readable outside the solar lobe.
        // Crossed, bounded layers deform locally; neither transports the whole ocean.
        float motionTime = time * max(waveSpeed, 0.0f);
        float motionScale = max(waveFrequency, 0.1f) / 5.4f;
        float2 surfacePosition = input.worldPos.xz * motionScale;
        float surfaceA = WaterPatch(surfacePosition * float2(0.16f, 0.32f)
            + SeaOrbit(motionTime, 0.9f) * 1.1f).x;
        float surfaceB = WaterPatch(float2(surfacePosition.y, -surfacePosition.x)
            * float2(0.23f, 0.11f) + SeaOrbit(motionTime * 0.83f, 3.7f) * 1.2f + 13.4f).x;
        float surfacePattern = surfaceA * 0.55f + surfaceB * 0.45f;
        float surfaceContrast = min(max(billboardScale, 0.0f), 2.0f);
        body *= 1.0f + (surfacePattern - 0.5f) * 0.65f * surfaceContrast;
    }

    // The old full-color texture is only a subtle optional subsurface variation.
    float2 flow = float2(uvOffsetX, uvOffsetY);
    if (effectType >= 1.5f)
        flow = SeaOrbit(time * max(waveSpeed, 0.0f) * 0.35f, 1.8f) * 0.035f;
    float2 bodyUV = MirrorUV(input.worldPos.xz * 0.011f + flow * 0.6f + slope * 0.04f);
    // Only retain broad color variation, not the photograph's tiny contour highlights.
    float bodyMip = max(authoredWaterSurfaceTex.CalculateLevelOfDetail(smp, bodyUV),
        effectType >= 1.5f ? 5.0f : 4.0f);
    float3 textureColor = authoredWaterSurfaceTex.SampleLevel(smp, bodyUV, bodyMip).rgb;
    float textureLuma = dot(textureColor, float3(0.2126f, 0.7152f, 0.0722f));
    body *= 1.0f + (textureLuma - 0.35f) * min(max(billboardScale, 0.0f), 2.0f) * 0.16f;

    float3 reflected = SkyRadiance(reflect(-view, normal));
    float3 result = lerp(body, reflected, fresnel);

    // Normal-driven solar reflection replaces painted white ribbons.
    float3 light = SafeUnit(-waterLightDirection, float3(0.35f, 0.82f, -0.45f));
    float3 halfVector = SafeUnit(light + view, normal);
    float NdotL = saturate(dot(normal, light));
    float NdotH = saturate(dot(normal, halfVector));
    float VdotH = saturate(dot(view, halfVector));
    float roughness = effectType >= 1.5f ? 0.14f : 0.075f;
    float a2 = roughness * roughness + ripples.z;
    // Broaden the lobe by pixel normal variance to prevent sparkling aliasing.
    a2 += 0.25f * (dot(ddx(normal), ddx(normal)) + dot(ddy(normal), ddy(normal)));
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    float distribution = a2 / max(3.14159265f * denom * denom, 0.00001f);
    float k = sqrt(a2) * 0.5f;
    float geometry = NdotV / max(NdotV * (1.0f - k) + k, 0.0001f);
    geometry *= NdotL / max(NdotL * (1.0f - k) + k, 0.0001f);
    float lightFresnel = 0.02037f + 0.97963f * pow(1.0f - VdotH, 5.0f);
    float specular = distribution * geometry * lightFresnel / max(4.0f * NdotV, 0.001f);
    // Compress the solar lobe into soft-edged, bounded accents instead of milky ribbons.
    float highlightWidth = clamp(fwidth(specular), 0.015f, 0.06f);
    float highlight = smoothstep(0.10f - highlightWidth, 0.22f + highlightWidth, specular);
    // Broad, bounded glints rather than opaque noise blobs or a white solar stripe.
    if (effectType >= 1.5f) highlight = smoothstep(0.025f - highlightWidth, 0.13f + highlightWidth, specular);
    float footprint = max(length(ddx(input.worldPos.xz)), length(ddy(input.worldPos.xz)));
    highlight *= lerp(1.0f, 0.35f, smoothstep(0.5f, 3.0f, footprint));
    // Break long solar ribbons into travelling patches without introducing glitter noise.
    float lightPhase = dot(input.worldPos.xz, float2(-0.073f, 0.119f))
        - time * max(waveSpeed, 0.0f) * 0.65f;
    float patchSignal = sin(lightPhase) * exp(-0.5f * fwidth(lightPhase) * fwidth(lightPhase));
    float lightPatches = smoothstep(-0.20f, 0.65f, patchSignal);
    if (effectType < 1.5f) {
        highlight *= lightPatches;
        result += ToLinear(waterLightColor) * max(waterLightIntensity, 0.0f)
            * max(effectScaleY, 0.0f) * (min(specular, 0.10f) * 0.08f + highlight * 0.42f);
    }
    if (effectType >= 1.5f) {
        float t = time * max(waveSpeed, 0.0f);
        float patternScale = max(waveFrequency, 0.1f) / 5.4f;
        float2 p = input.worldPos.xz * patternScale;
        float broad = SunlitFacets(p * 0.065f, t * 0.65f);
        float medium = SunlitFacets(p * 0.63f + float2(17.3f, -9.2f), t * 0.90f);
        float fine = SunlitFacets(p * 2.7f + float2(-5.7f, 23.1f), t);
        float detail = saturate(topDownDetail);
        // A broad sun-facing reflection field contains discrete bright facets.
        // The angular envelope follows the actual sun and camera, never the camera alone.
        float sunField = pow(saturate(halfVector.y), 6.0f) * saturate(light.y);
        // Leave blue negative space. Fine glints live inside a broad soft reflection.
        float cluster = smoothstep(0.38f, 0.72f, broad);
        float midFacet = smoothstep(0.36f, 0.72f, medium);
        float fineWidth = max(fwidth(fine), 0.06f);
        float fineFacet = smoothstep(0.62f - fineWidth, 0.78f + fineWidth, fine);
        float fineFade = 1.0f - smoothstep(0.15f, 0.75f, footprint * patternScale);
        // Broad travelling reflections survive when only fine ripples are disabled.
        float facets = cluster * (0.10f + midFacet * 0.58f
            + fineFacet * fineFade * 0.08f * detail);
        float solarCore = specular / (0.16f + specular);
        float solar = sunField * facets + solarCore * (0.045f + cluster * midFacet * 0.16f);
        result += ToLinear(waterLightColor) * max(waterLightIntensity, 0.0f)
            * max(effectScaleY, 0.0f) * solar;
        // A grazing view approaches the reflected sky, not a flat cyan horizon.
        float aerial = smoothstep(100.0f, 650.0f, length(cameraWorldPosition - input.worldPos))
            * pow(1.0f - saturate(view.y), 3.0f) * 0.28f;
        result = lerp(result, reflected, aerial);
    }

    // Foam only exists at a real depth intersection, not every screen-space silhouette.
    float foamWidth = lerp(0.15f, 2.5f, saturate(effectSoftness));
    float contact = hasBottom ? 1.0f - smoothstep(0.0f, foamWidth, thickness) : 0.0f;
    // Low-frequency moving gaps keep contact foam legible in a stylized scene.
    float foamTime = time * max(waveSpeed, 0.0f);
    float shorePhase = dot(input.worldPos.xz, float2(0.23f, 0.17f)) - foamTime * 0.8f;
    float shoreSignal = sin(shorePhase) * exp(-0.5f * fwidth(shorePhase) * fwidth(shorePhase));
    float foamGaps = smoothstep(-0.65f, 0.35f, shoreSignal);
    float surge = 0.65f + 0.18f * sin(foamTime * 1.2f + dot(input.worldPos.xz, float2(0.035f, -0.027f)));
    float foamEdge = max(fwidth(thickness), 0.05f);
    float foamBand = 1.0f - smoothstep(foamWidth * surge - foamEdge,
        foamWidth * surge + foamEdge, thickness);
    // A translucent aqua transition anchors genuine intersections without outlining objects.
    float contactTint = hasBottom ? 1.0f - smoothstep(0.0f, foamWidth * 2.0f, thickness) : 0.0f;
    result = lerp(result, float3(0.045f, 0.42f, 0.40f), contactTint * 0.22f);
    float foam = saturate(contact * foamBand * foamGaps * 0.72f * max(effectScaleZ, 0.0f));
    result = lerp(result, float3(0.85f, 0.94f, 0.96f), foam);
    result *= max(effectIntensity, 0.0f);

    // Refraction is already composited; alpha must not blend the background twice.
    // Artist opacity fades the final composite, rather than reducing water transmission.
    float3 original = grabTex.Sample(smp, uv).rgb;
    return float4(lerp(original, max(result, 0.0f), saturate(color.a)), 1.0f);
}
