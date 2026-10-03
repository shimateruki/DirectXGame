#ifndef FOLIAGE_DEFORMATION_HLSLI
#define FOLIAGE_DEFORMATION_HLSLI

// Same prefix/layout as the pixel material buffer. No additional CPU allocation.
cbuffer FoliageMaterial : register(b6)
{
    float4 foliageColor;
    int foliageLighting; float3 foliagePadding;
    float4x4 foliageUV;
    int foliageSelectedLighting; float foliageShininess;
    int foliageType; float foliageRoughness;
    float foliageMetallic; int foliageNormalMap; int foliageEnvMap;
    float foliageWindStrength;
    float foliageEmissive; float foliageTime;
};

cbuffer GrassContact : register(b7) { float4 grassContacts[8]; };

float3 BendNormal(float3 n, float2 direction, float angle)
{
    float parallel = dot(n.xz, direction);
    float s = sin(angle), c = cos(angle);
    return float3(n.x + direction.x * (parallel * (c - 1) + n.y * s),
        n.y * c - parallel * s,
        n.z + direction.y * (parallel * (c - 1) + n.y * s));
}

// Share identical deformation between color and shadow passes.
void ApplyFoliageDeformation(inout float4 skinnedPosition, inout float3 skinnedNormal,
    inout float3 skinnedTangent, float2 texcoord, float4x4 world, float4x4 worldInverseTranspose)
{
    if (foliageType == 30) {
        float3 rootWorld = mul(float4(skinnedPosition.x, 0, skinnedPosition.z, 1),
            world).xyz;
        // Imported blade UV height is independent of the authored blade scale.
        float tip = saturate(texcoord.y);
        float phase = dot(rootWorld.xz, float2(0.19f, 0.13f));
        float gust = 0.55f + 0.35f * sin(foliageTime * 0.63f - phase);
        float wind = gust * sin(foliageTime * 1.35f + phase * 0.8f);
        float flutter = sin(foliageTime * 3.1f + phase * 4.7f) * 0.035f * tip;
        float2 bend = (float2(0.38f * wind, 0.16f * sin(foliageTime * 1.08f + phase))
            + flutter) * clamp(foliageWindStrength, 0.0f, 2.0f);
        float2 pushWorld = 0;
        [unroll] for (int contact = 0; contact < 8; ++contact) {
            float2 away = rootWorld.xz - grassContacts[contact].xz;
            float distance = length(away);
            float strength = (1.0f - smoothstep(0.25f, 2.1f, distance))
                * (1.0f - smoothstep(0.8f, 2.6f, abs(rootWorld.y - grassContacts[contact].y)))
                * grassContacts[contact].w;
            pushWorld += away / max(distance, 0.05f) * strength;
        }
        pushWorld /= max(1.0f, length(pushWorld));
        float3 localPush = mul(float3(pushWorld.x, 0, pushWorld.y),
            (float3x3)transpose(worldInverseTranspose));
        bend += localPush.xz * 1.05f;
        float curvature = min(length(bend), 1.2f);
        float2 bendDirection = bend / max(length(bend), 0.0001f);
        float bladeHeight = skinnedPosition.y / max(tip, 0.001f);
        float angle = curvature * tip;
        // Constant-curvature arc: bend the blade rather than stretching sideways.
        skinnedPosition.xz += bendDirection * bladeHeight
            * (1.0f - cos(angle)) / max(curvature, 0.0001f);
        skinnedPosition.y = curvature > 0.0001f
            ? bladeHeight * sin(angle) / curvature : skinnedPosition.y;
        skinnedNormal = BendNormal(skinnedNormal, bendDirection, angle);
        skinnedTangent = BendNormal(skinnedTangent, bendDirection, angle);
    }
    if (foliageType == 31) {
        // Each leaf has a fixed base and a separate phase encoded in UV.x.
        float leafMask = step(0.5f, texcoord.y);
        float tip = saturate((texcoord.y - 0.55f) / 0.45f);
        float phase = texcoord.x * 6.2831853f;
        float sway = sin(foliageTime * 1.15f + skinnedPosition.y * 0.7f);
        float flutter = sin(foliageTime * 3.6f + phase);
        float bend = (sway * 0.035f + flutter * 0.022f)
            * tip * tip * leafMask * clamp(foliageWindStrength, 0.0f, 2.0f);
        skinnedPosition.xyz += float3(0.65f, 0.75f, 0.25f) * bend;
        skinnedNormal = BendNormal(skinnedNormal, normalize(float2(0.65f,0.25f)), bend * 3.0f);
        skinnedTangent = BendNormal(skinnedTangent, normalize(float2(0.65f,0.25f)), bend * 3.0f);
    }
}
#endif
