struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 projPos : TEXCOORD1; // Projection position for soft particles.
};

cbuffer CameraData : register(b0)
{
    row_major matrix viewProj;
    row_major matrix billboardMatrix;
    row_major matrix projection;
    float softParticleFade;
    int blendMode; 
    float2 screenSize; 
    uint spriteSheetColumns;
    uint spriteSheetRows;
    uint spriteSheetFrameCount;
    float spriteSheetFps;
    uint spriteSheetLoop;
    uint spriteSheetRandomStart;
    uint alignToVelocity;
    float velocityStretch;
    uint particleType;
    float trailLength;
    uint receiveLighting;
    float lightingStrength;
    float3 lightDirection;
    float cameraPadding0;
    float3 lightColor;
    float cameraPadding1;
};

Texture2D tex : register(t1);
Texture2D<float> depthTex : register(t2);
Texture2D grabTex : register(t3); // Copied scene background.

SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET
{
    // Compute the soft-particle fade.
    // The reciprocal of SV_POSITION.w gives the particle linear depth here.
    float linearParticleDepth = 1.0f / input.pos.w;
    float bgDepthZ = depthTex.Load(int3(input.pos.xy, 0));
    float m22 = projection._m22;
    float m32 = projection._m32;
    float linearBgDepth = m32 / (bgDepthZ - m22);
    float depthDiff = linearBgDepth - linearParticleDepth;
    float softFactor = saturate(depthDiff / softParticleFade);

    float4 finalColor;

    if (blendMode == 2)
    {
        // =======================================================
        // Distortion pass with particle tint support.
        // =======================================================
        float4 texColor = tex.Sample(smp, input.uv);
        
        // Compute the distortion vector.
        float2 offset = (texColor.rg - 0.5f) * 2.0f;
        offset *= input.color.r * input.color.a * 0.1f * softFactor;
        
        // Sample the distorted scene background.
        float2 screenUV = input.pos.xy / screenSize;
        float3 distortedBg = grabTex.Sample(smp, screenUV + offset).rgb;
        
        // Blend a subtle particle tint into the distorted background.
        // Alpha controls how strongly the configured tint affects the scene.
        float3 tintColor = input.color.rgb * texColor.rgb;
        float3 finalRGB = lerp(distortedBg, distortedBg + tintColor, input.color.a * 0.5f);
        
        finalColor = float4(finalRGB, input.color.a * texColor.a * softFactor);
    }
    else
    {
        // Standard additive or alpha-blended rendering.
        float4 texColor = tex.Sample(smp, input.uv);
        finalColor = input.color * texColor;
        finalColor.a *= softFactor;
    }

    if (receiveLighting != uint(0) && blendMode != 2)
    {
        float2 normalXY = input.uv * 2.0f - 1.0f;
        float normalZ = sqrt(saturate(1.0f - dot(normalXY, normalXY)));
        float3 normal = normalize(float3(normalXY.x, -normalXY.y, normalZ));
        float lightDirectionLength = length(lightDirection);
        float3 direction = lightDirectionLength > 0.0001f
            ? -lightDirection / lightDirectionLength
            : float3(0.0f, 1.0f, 0.0f);
        float diffuse = 0.28f + 0.72f * saturate(dot(normal, direction));
        float3 litColor = finalColor.rgb * lightColor * diffuse;
        finalColor.rgb = lerp(finalColor.rgb, litColor, saturate(lightingStrength));
    }

    if (finalColor.a <= 0.0f)
        discard;

    return finalColor;
}
