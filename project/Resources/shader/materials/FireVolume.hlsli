// Real 3D density inside a closed proxy. Shared by braziers and slime head flames.
// No near/far constants: scene depth is reconstructed with the current camera.
float3 FireWorldPosition(float2 uv, float depth)
{
    float4 p = mul(float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f),
                   waterInverseViewProjection);
    return p.xyz / p.w;
}

float FireHash3(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float FireNoise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(
        lerp(lerp(FireHash3(i), FireHash3(i + float3(1,0,0)), f.x),
             lerp(FireHash3(i + float3(0,1,0)), FireHash3(i + float3(1,1,0)), f.x), f.y),
        lerp(lerp(FireHash3(i + float3(0,0,1)), FireHash3(i + float3(1,0,1)), f.x),
             lerp(FireHash3(i + float3(0,1,1)), FireHash3(i + float3(1,1,1)), f.x), f.y), f.z);
}

float2 FireDensityHeat(float3 p, float phase)
{
    float h = p.y * 0.5f + 0.5f;
    float t = time * max(waveSpeed, 0.0f);
    float movement = 0.65f + saturate(abs(flowSpeedY)) * 0.35f;
    float turbulence = clamp(waveHeight, 0.0f, 1.5f);
    float2 bend = float2(sin(t * 1.7f - h * 4.2f + phase),
                         cos(t * 1.31f - h * 5.0f + phase * 1.7f));
    p.xz -= bend * h * h * (0.08f + turbulence * 0.10f) * movement;
    p.x -= clamp(flowSpeedX, -1.0f, 1.0f) * h * h * 0.22f;

    float3 flow = p * float3(3.4f, 3.0f, 3.4f) * clamp(waveFrequency * 0.45f, 0.45f, 2.4f);
    flow += float3(phase, -t * 1.6f, phase * 0.37f);
    float broad = FireNoise3(flow);
    float fine = FireNoise3(flow * 2.03f + float3(13.1f, -t * 0.7f, 7.3f));
    float noise = broad * 0.72f + fine * 0.28f;

    // A coherent body plus two off-axis tongues creates parallax at every angle.
    // Braziers breathe gently; independent tongues avoid a single candle silhouette.
    float brazier = effectType > 3.5f ? 1.0f : 0.0f;
    float crownHeight = 0.93f + brazier * 0.035f * sin(t * 2.3f + phase);
    float radius = (0.48f + brazier * 0.045f) * pow(saturate(1.0f - h / crownHeight), 0.85f);
    radius *= 0.78f + 0.22f * smoothstep(0.01f, 0.20f, h);
    float radial = length(p.xz);
    float field = radius - radial;
    float2 sideA = float2(0.25f + h * 0.16f, 0.14f) + bend * h * 0.09f;
    float2 sideB = float2(-0.23f - h * 0.12f, -0.20f) - bend.yx * h * 0.08f;
    float heightA = 0.78f + brazier * 0.10f * sin(t * 2.7f + phase + 1.2f);
    float heightB = 0.65f + brazier * 0.13f * sin(t * 1.9f + phase * 1.3f);
    float tongueA = 0.27f * pow(saturate(1.0f - h / heightA), 0.65f) - length(p.xz - sideA);
    float tongueB = 0.25f * pow(saturate(1.0f - h / heightB), 0.65f) - length(p.xz - sideB);
    tongueA -= smoothstep(heightA - 0.06f, heightA + 0.02f, h);
    tongueB -= smoothstep(heightB - 0.06f, heightB + 0.02f, h);
    field = max(field, max(tongueA, tongueB));
    field += (noise - 0.5f) * (0.12f + turbulence * 0.28f) * (0.30f + h);
    float softness = lerp(0.025f, 0.14f, saturate(effectSoftness));
    float density = smoothstep(-softness, softness + 0.075f, field);
    density *= smoothstep(0.005f, 0.08f, h) * (1.0f - smoothstep(0.86f, 0.99f, h));
    density *= smoothstep(0.20f, 0.62f, noise + saturate(field * 4.0f) * 0.18f);
    // Heat falls across the cross section as well as upwards, not painted onto a face.
    float heat = saturate(0.28f + saturate(field / max(radius, 0.06f)) * 0.68f
                         + (1.0f - h) * 0.22f + (noise - 0.5f) * 0.28f);
    return float2(density, heat);
}

float2 FireBreathDensityHeat(float3 p, float phase)
{
    // Local +Z follows the breath direction; XY forms a real cross section.
    float progress = saturate(p.z * 0.5f + 0.5f);
    float t = time * max(waveSpeed, 0.0f);
    float3 flow = p * float3(3.1f, 3.1f, 2.6f);
    flow += float3(phase, phase * 0.37f, -t * 1.7f);
    float broad = FireNoise3(flow);
    float fine = FireNoise3(flow * 2.11f + float3(8.1f, 13.7f, -t * 0.9f));
    float turbulence = broad * 0.72f + fine * 0.28f;
    float2 curl = float2(
        sin(t * 1.8f - p.z * 3.4f + phase),
        cos(t * 1.4f - p.z * 4.1f + phase * 0.8f)) * 0.07f;
    float radial = length(p.xy - curl);
    float radius = lerp(0.43f, 0.68f, progress);
    float field = radius - radial + (turbulence - 0.5f) * 0.22f;
    float softness = lerp(0.035f, 0.13f, saturate(effectSoftness));
    float density = smoothstep(-softness, softness + 0.06f, field);
    density *= smoothstep(-0.97f, -0.66f, p.z) *
               (1.0f - smoothstep(0.67f, 0.99f, p.z));
    density *= smoothstep(0.21f, 0.59f, turbulence + saturate(field * 3.0f) * 0.18f);
    float heat = saturate(0.30f + saturate(field / max(radius, 0.05f)) * 0.56f +
                          progress * 0.11f + (turbulence - 0.5f) * 0.28f);
    return float2(density, heat);
}

float2 FirePillarDensityHeat(float3 p, float phase)
{
    float h = saturate(p.y * 0.5f + 0.5f);
    float t = time * max(waveSpeed, 0.0f);
    float3 flow = p * float3(3.3f, 2.9f, 3.3f);
    flow += float3(phase, -t * 1.05f, phase * 0.41f);
    float broad = FireNoise3(flow);
    float detail = FireNoise3(flow * 2.13f + float3(7.1f, -t * 0.51f, 11.9f));
    float angle = atan2(p.z, p.x);
    float spiral = sin(angle * 7.0f - h * 15.0f + t * 1.1f + phase);
    float counterSpiral = sin(angle * 11.0f + h * 10.0f - t * 0.74f);
    float2 curl = float2(sin(t * 0.75f - h * 4.4f + phase),
                         cos(t * 0.68f - h * 4.7f + phase)) * (0.025f + h * 0.055f);
    // A luminous cylindrical shell and a faint interior preserve depth cues.
    float radius = lerp(0.64f, 0.61f, h);
    float radial = length(p.xz - curl);
    float rimOffset = (broad - 0.5f) * 0.13f + spiral * 0.035f;
    float shellDistance = (radial - radius + rimOffset) * 7.5f;
    float shell = exp(-shellDistance * shellDistance);
    float interior = 1.0f - smoothstep(radius - 0.13f, radius + 0.03f, radial);
    float streak = pow(saturate(0.5f + 0.5f *
        sin(angle * 8.0f - h * 19.0f + t * 1.13f + detail * 1.8f)), 7.0f);
    float density = shell * (1.35f + streak * 0.72f) * (0.66f + broad * 0.36f) +
        interior * (0.045f + detail * 0.055f);
    // The crown varies around the circumference without tapering the shaft.
    float crest = 0.90f + (broad - 0.5f) * 0.12f + spiral * 0.055f;
    density *= smoothstep(0.0f, 0.035f, h) *
        (1.0f - smoothstep(crest - 0.10f, crest + 0.09f, h));
    // The caster drives effectSoftness from zero to one during release.
    // Higher wisps and bright streaks survive after the base breaks apart.
    float survival = h * 0.50f + broad * 0.40f + streak * 0.10f;
    float dissolve = saturate(effectSoftness);
    density *= smoothstep(dissolve * 1.25f - 0.15f,
                          dissolve * 1.25f + 0.05f, survival);
    float heat = saturate(0.17f + shell * 0.20f + streak * 0.20f +
                          (1.0f - h) * 0.10f + (broad - 0.5f) * 0.19f);
    return float2(density, heat);
}

float2 HellGateFanDensityHeat(float3 p, float phase)
{
    // Local +Z points away from the caster. The wedge and the reveal share that axis.
    float distance01 = saturate(p.z * 0.5f + 0.5f);
    float height01 = saturate(p.y * 0.5f + 0.5f);
    float halfWidth = lerp(0.056f, 0.96f, distance01);
    float side = abs(p.x) / max(halfWidth, 0.01f);
    float wedge = 1.0f - smoothstep(0.88f, 1.08f, side);
    float reveal = 1.0f - smoothstep(effectSoftness - 0.09f,
                                     effectSoftness + 0.04f, distance01);
    float growth = saturate((effectSoftness - distance01) * 6.0f + 0.12f);
    float t = time * max(waveSpeed, 0.05f);
    float3 flow = p * float3(8.0f, 3.0f, 7.0f) + float3(phase, -t * 1.5f, phase * 0.41f);
    float broad = FireNoise3(flow);
    float detail = FireNoise3(flow * 2.17f + float3(7.2f, -t * 0.73f, 11.4f));
    float flicker = broad * 0.72f + detail * 0.28f;
    float crest = (0.77f + (broad - 0.5f) * 0.29f +
                   sin(p.x * 19.0f + p.z * 11.0f - t * 1.8f) * 0.075f) * growth;
    float vertical = smoothstep(0.0f, 0.055f, height01) *
                     (1.0f - smoothstep(crest - 0.14f, crest + 0.07f, height01));
    float density = wedge * reveal * vertical * (0.52f + flicker * 0.80f);
    float lick = pow(saturate(0.5f + 0.5f *
        sin(p.x * 25.0f + p.z * 17.0f + height01 * 13.0f - t * 2.3f)), 6.0f);
    density *= 0.72f + lick * 0.46f;
    // Narrow cyan tongues travel through the violet body rather than tinting every flame.
    float cyanTongue = pow(saturate(0.5f + 0.5f *
        sin(p.x * 12.0f - p.z * 18.0f + height01 * 7.0f - t * 1.9f + broad)), 10.0f);
    float heat = saturate(0.23f + (1.0f - height01) * 0.27f +
                          flicker * 0.34f + lick * 0.12f + cyanTongue * 0.58f);
    return float2(density, heat);
}

float3 FirePillarColor(float heat)
{
    float3 ember = float3(0.38f, 0.016f, 0.003f);
    float3 orange = float3(1.35f, 0.23f, 0.018f);
    float3 hot = float3(2.05f, 0.71f, 0.090f);
    float3 c = lerp(ember, orange, smoothstep(0.14f, 0.53f, heat));
    c = lerp(c, hot, smoothstep(0.56f, 0.88f, heat) * 0.65f);
    float3 tint = max(color.rgb, 0.001f);
    tint /= max(max(tint.r, tint.g), tint.b);
    return c * lerp(1.0f, tint, 0.10f) * max(effectIntensity, 0.0f);
}

float3 HellGateColor(float heat)
{
    float3 shadow = float3(0.13f, 0.015f, 0.31f);
    float3 violet = float3(0.73f, 0.12f, 1.48f);
    float3 cyan = float3(0.24f, 1.42f, 1.95f);
    float3 c = lerp(shadow, violet, smoothstep(0.12f, 0.55f, heat));
    c = lerp(c, cyan, smoothstep(0.73f, 0.96f, heat) * 0.82f);
    return c * max(effectIntensity, 0.0f);
}

float3 FireVolumeColor(float heat)
{
    float3 c = lerp(float3(1.05f, 0.095f, 0.009f), float3(1.85f, 0.39f, 0.025f),
                    smoothstep(0.22f, 0.63f, heat));
    c = lerp(c, float3(3.05f, 1.68f, 0.28f), smoothstep(0.58f, 0.88f, heat));
    c = lerp(c, float3(2.75f, 1.90f, 0.65f), smoothstep(0.91f, 1.0f, heat) * 0.40f);
    // Keep Inspector tint meaningful without multiplying the fire into pure red.
    float3 tint = max(color.rgb, 0.001f);
    tint /= max(max(tint.r, tint.g), tint.b);
    return c * lerp(1.0f, tint, 0.14f) * max(effectIntensity, 0.0f);
}

float4 RenderFireVolume(VSOutput input)
{
    uint width, height;
    depthTex.GetDimensions(width, height);
    float2 uv = input.pos.xy / float2(width, height);
    float depth = depthTex.Load(int3(input.pos.xy, 0));
    float3 nearPoint = FireWorldPosition(uv, 0.0f);
    float3 farPoint = FireWorldPosition(uv, 1.0f);
    float3 ray = normalize(farPoint - nearPoint);
    float3 center = mul(float4(0,0,0,1), world).xyz;
    float3 extent = max(billboardScale, 0.05f) * max(effectScale, 0.05f) *
                    max(float3(effectScaleX, effectScaleY, effectScaleZ), 0.12f);
    float3x3 invWorld = transpose((float3x3)WorldInverseTranspose);
    float3 origin = mul(nearPoint - center, invWorld) / extent;
    float3 direction = mul(ray, invWorld) / extent;

    // Exit faces cover the volume even when the camera or near plane is inside.
    // Use the supplied face normal, independent of triangle winding and mirrored scales.
    if (dot(input.normal, ray) <= 0.0f) discard;
    float3 safeDirection = float3(
        abs(direction.x) < 1e-6f ? (direction.x < 0 ? -1e-6f : 1e-6f) : direction.x,
        abs(direction.y) < 1e-6f ? (direction.y < 0 ? -1e-6f : 1e-6f) : direction.y,
        abs(direction.z) < 1e-6f ? (direction.z < 0 ? -1e-6f : 1e-6f) : direction.z);
    float3 a = (-1.0f - origin) / safeDirection;
    float3 b = ( 1.0f - origin) / safeDirection;
    float3 lo = min(a, b), hi = max(a, b);
    float start = max(max(lo.x, max(lo.y, lo.z)), 0.0f);
    float finish = min(hi.x, min(hi.y, hi.z));
    // Stop exactly at opaque geometry. Do not discard an entire volume when it intersects.
    if (depth < 1.0f)
        finish = min(finish, dot(FireWorldPosition(uv, depth) - nearPoint, ray));
    if (finish <= start) discard;

    const int steps = effectType > 4.5f && effectType < 5.5f ? 24 : 32;
    float stepLength = (finish - start) / steps;
    float localStep = stepLength * length(direction);
    float phase = dot(center, float3(0.173f, 0.071f, 0.137f));
    float3 accumulated = 0.0f;
    float transmittance = 1.0f;
    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float3 p = origin + direction * (start + (i + 0.5f) * stepLength);
        float2 sampleValue = effectType > 6.5f ? HellGateFanDensityHeat(p, phase) :
            (effectType > 5.5f ? FirePillarDensityHeat(p, phase) :
             (effectType > 4.5f ? FireBreathDensityHeat(p, phase) :
              FireDensityHeat(p, phase)));
        float opacityScale = effectType > 6.5f ? 2.30f :
            (effectType > 5.5f ? 1.85f : 2.7f);
        float opacity = 1.0f - exp(-sampleValue.x * localStep * opacityScale);
        float weight = transmittance * opacity;
        accumulated += weight * (effectType > 6.5f ? HellGateColor(sampleValue.y) :
            (effectType > 5.5f ? FirePillarColor(sampleValue.y) : FireVolumeColor(sampleValue.y)));
        transmittance *= 1.0f - opacity;
        if (transmittance < 0.025f) break;
    }
    float alpha = 1.0f - transmittance;
    if (alpha * saturate(color.a) < 0.005f) discard;
    // The existing normal blend expects straight alpha, not premultiplied emission.
    return float4(accumulated / max(alpha, 0.0001f), alpha * saturate(color.a));
}
