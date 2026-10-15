#ifndef OBJECT3D_MATERIAL_VARIANTS_HLSLI
#define OBJECT3D_MATERIAL_VARIANTS_HLSLI

// Keep the shared scene lighting and fog in Object3d.PS.hlsl; only material color belongs here.
float4 ShadeDashPanel(VertexShaderOutput input, float2 uv, float4 textureColor, float shadowFactor)
{
    float3 dashNormal = normalize(input.normal);
    float3 rgb = BuildDashPanelColor(uv, textureColor.rgb, dashNormal, shadowFactor);
    return float4(rgb, MATERIAL_COLOR.a * textureColor.a);
}

float4 ShadeSurfaceDecal(float4 textureColor)
{
    float4 color = MATERIAL_COLOR * textureColor;
    color.rgb *= gMaterial.emissive;
    return color;
}

#endif
