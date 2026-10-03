#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

float random(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}
float noise(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);
    float a = random(i);
    float b = random(i + float2(1.0, 0.0));
    float c = random(i + float2(0.0, 1.0));
    float d = random(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float GlassSegmentDistance(float2 p, float2 a, float2 b)
{
    float2 ab = b - a;
    float2 offset = p - a - ab * saturate(dot(p - a, ab) / max(dot(ab, ab), 0.00001f));
    return dot(offset, offset);
}

float ReadGlassDepth(float2 uv)
{
    uint width, height;
    depthTex.GetDimensions(width, height);
    uint2 pixel = min(uint2(saturate(uv) * float2(width, height)), uint2(width - 1, height - 1));
    return depthTex.Load(int3(pixel, 0));
}

float4 DrawCrackedGlass(VSOutput input)
{
    // Opt-in glass mode; legacy ice keeps its original frost rendering.
    float2 uv = saturate(input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f);
    float surfaceDepth = input.screenPos.z / input.screenPos.w;
    clip(ReadGlassDepth(uv) - surfaceDepth + 0.000001f);
    float2 p = input.localPos.xy / max(effectScaleY, 0.01f);
    float2 origin = float2(-0.19f, 0.06f);
    float distanceToCrack = 10.0f;
    float distanceToBranch = 10.0f;

    // Unequal fracture paths avoid the regular spokes and complete rings of the old pattern.
    float2 a = float2(-0.52f, 0.32f);
    float2 b = float2(-0.86f, 0.42f);
    float2 c = float2(-1.10f, 0.68f);
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, origin, a));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, a, b));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, b, c));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, c, float2(-1.48f, 0.86f)));
    distanceToBranch = min(distanceToBranch, GlassSegmentDistance(p, b, float2(-0.82f, 0.64f)));

    a = float2(0.06f, 0.23f);
    b = float2(0.37f, 0.29f);
    c = float2(0.48f, 0.55f);
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, origin, a));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, a, b));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, b, c));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, c, float2(0.79f, 0.76f)));
    distanceToBranch = min(distanceToBranch, GlassSegmentDistance(p, b, float2(0.62f, 0.21f)));

    a = float2(0.18f, 0.01f);
    b = float2(0.54f, -0.07f);
    c = float2(0.94f, 0.10f);
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, origin, a));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, a, b));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, b, c));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, c, float2(1.43f, 0.14f)));
    distanceToBranch = min(distanceToBranch, GlassSegmentDistance(p, c, float2(1.10f, -0.13f)));

    a = float2(0.01f, -0.20f);
    b = float2(0.10f, -0.44f);
    c = float2(0.36f, -0.64f);
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, origin, a));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, a, b));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, b, c));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, c, float2(0.58f, -0.87f)));
    distanceToBranch = min(distanceToBranch, GlassSegmentDistance(p, b, float2(-0.08f, -0.55f)));

    a = float2(-0.38f, -0.13f);
    b = float2(-0.59f, -0.27f);
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, origin, a));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, a, b));
    distanceToCrack = min(distanceToCrack, GlassSegmentDistance(p, b, float2(-0.93f, -0.53f)));
    distanceToBranch = min(distanceToBranch, GlassSegmentDistance(p, b, float2(-0.78f, -0.15f)));

    distanceToCrack = sqrt(distanceToCrack);
    distanceToBranch = sqrt(distanceToBranch);
    float localFace = abs(normalize(cross(ddx(input.localPos), ddy(input.localPos))).z);
    float paneFace = step(0.75f, localFace);
    float crackAA = max(fwidth(distanceToCrack), 0.00045f);
    float branchAA = max(fwidth(distanceToBranch), 0.00045f);
    float crackCore = 1.0f - smoothstep(0.00065f, 0.00065f + crackAA, distanceToCrack);
    float branchCore = 1.0f - smoothstep(0.00045f, 0.00045f + branchAA, distanceToBranch);
    float fracture = saturate(crackCore + branchCore * 0.55f) * paneFace;
    float highlight = (1.0f - smoothstep(0.0018f, 0.0018f + crackAA, distanceToCrack)) * paneFace;
    highlight *= 1.0f - crackCore;
    float impact = 1.0f - smoothstep(0.018f, 0.042f, length(p - origin));
    impact *= paneFace;

    float3 N = normalize(input.normal);
    float3 V = normalize(cameraWorldPosition - input.worldPos);
    N *= dot(N, V) < 0.0f ? -1.0f : 1.0f;
    float ndv = saturate(dot(N, V));
    float fresnel = 0.04f + 0.96f * pow(1.0f - ndv, 5.0f);
    float2 grain = float2(noise(p * 9.7f), noise(p * 9.7f + 7.3f)) - 0.5f;
    float2 refractOffset = grain * (0.00035f + 0.0007f * max(effectScale, 0.0f));
    // The screen-color copy supports a restrained refraction approximation, not a true ray trace.
    refractOffset += float2(V.x, -V.y) * (1.0f - ndv) * max(effectScale, 0.0f) * 0.0025f;
    refractOffset += float2(0.0018f, -0.0010f) * saturate(fracture + impact);
    float2 refractedUV = saturate(uv + refractOffset);
    float refractedDepth = ReadGlassDepth(refractedUV);
    if (refractedDepth < surfaceDepth || refractedDepth >= 0.999999f)
        refractedUV = uv;
    float3 transmitted = grabTex.Sample(smp, refractedUV).rgb;
    float edge = 1.0f - smoothstep(0.0f, 0.045f,
        min(1.0f - abs(p.y), (effectScaleX - abs(input.localPos.x)) / max(effectScaleY, 0.01f)));
    float raisedTrim = smoothstep(effectScaleZ + 0.05f, effectScaleZ + 0.20f, input.localPos.z);
    float frame = saturate(raisedTrim + (1.0f - paneFace) * 0.65f + edge * 0.45f);
    float3 result = lerp(transmitted, color.rgb, saturate(color.a) * 0.55f + 0.025f);
    // A view-dependent ambient response provides readable reflections without a cubemap.
    float3 reflectedDirection = reflect(-V, N);
    float horizon = saturate(reflectedDirection.y * 0.5f + 0.5f);
    float3 ambientReflection = lerp(float3(0.10f, 0.15f, 0.19f),
        float3(0.40f, 0.50f, 0.55f), horizon);
    result = lerp(result, ambientReflection, fresnel * (0.45f + 0.35f * saturate(effectSoftness)));
    float3 light = normalize(-waterLightDirection);
    float3 halfVector = normalize(light + V);
    float specular = pow(saturate(dot(N, halfVector)), 64.0f);
    result += waterLightColor * min(waterLightIntensity, 2.0f) * specular * 0.13f;
    // The raised bars and side faces are solid trim; the large flat face remains transmissive.
    float3 trimColor = lerp(float3(0.18f, 0.24f, 0.25f), color.rgb, 0.45f);
    result = lerp(result, trimColor + specular * waterLightColor * 0.18f, frame * 0.72f);
    // A dark fissure and its narrow reflected edge read as broken glass without bloom.
    result *= 1.0f - fracture * saturate(effectIntensity) * 0.56f;
    result += float3(0.20f, 0.24f, 0.25f) * highlight * saturate(effectIntensity);
    result += float3(0.25f, 0.30f, 0.32f) * impact * saturate(effectIntensity) * 0.35f;
    return float4(result, 1.0f);
}

float4 main(VSOutput input) : SV_TARGET
{
    if (effectType > 0.5f && effectType < 1.5f)
        return DrawCrackedGlass(input);
    // =========================================================
    // 1. マグマで大成功した「真の法線」の計算（シマシマ撲滅）
    // =========================================================
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 flatNormal = abs(normalize(cross(dx, dy)));

    float2 baseUV;
    if (flatNormal.y > 0.5f)
    {
        baseUV = input.worldPos.xz;
    }
    else if (flatNormal.x > 0.5f)
    {
        baseUV = input.worldPos.zy;
    }
    else
    {
        baseUV = input.worldPos.xy;
    }

    // =========================================================
    // 2. 氷のヒビ・霜（フロスト）のノイズ
    // =========================================================
    // 氷なので時間は使わず、座標だけで静的な模様を作る
    float frost = noise(baseUV * 3.0f);
    float fineFrost = noise(baseUV * 12.0f);
    // 大小2つのノイズを混ぜて、リアルな霜のムラを作る
    float combinedFrost = (frost + fineFrost * 0.5f) / 1.5f;

    // =========================================================
    // 3. 屈折（Refraction）の計算
    // =========================================================
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV.x = screenUV.x * 0.5f + 0.5f;
    screenUV.y = -screenUV.y * 0.5f + 0.5f;

    // 霜のノイズを使って、背景（向こう側の景色）を強く歪ませる
    float2 distortedUV = screenUV + (combinedFrost * 0.06f) - 0.03f;
    float3 refractionColor = grabTex.Sample(smp, distortedUV).rgb;

    // =========================================================
    // 4. ライティングとフレネル（クリスタル感）
    // =========================================================
    // 光の計算には、カクカクのflatNormalではなく元の滑らかな法線を使うと綺麗になる
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(float3(0.0f, 0.5f, -1.0f));
    float fresnel = pow(1.0f - max(dot(viewDir, normal), 0.0f), 2.0f);

    // =========================================================
    // 5. 最終的な色の合成
    // =========================================================
    // Inspectorの色(color)に霜の白さを混ぜる
    float3 iceColor = lerp(color.rgb, float3(1.0f, 1.0f, 1.0f), combinedFrost * 0.6f);

    float4 finalColor;
    // 歪ませた背景色と、氷の色をアルファ値(color.a)でブレンド
    finalColor.rgb = lerp(refractionColor, iceColor, color.a);

    // 縁（エッジ）をフレネルで白く光らせて、硬い氷の質感を出す
    finalColor.rgb += float3(1.0f, 1.0f, 1.0f) * fresnel * 0.5f;

    // 背景を透かす処理（屈折）は自前でやっているので、出力アルファは1.0でOK
    finalColor.a = 1.0f;

    return finalColor;
}
