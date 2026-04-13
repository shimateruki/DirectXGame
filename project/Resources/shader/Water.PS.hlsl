#include "Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

// ==========================================
// 自動生成ノイズ関数（テクスチャ不要！）
// ==========================================
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

float LinearizeDepth(float z)
{
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    return (nearClip * farClip) / (farClip - z * (farClip - nearClip));
}

float4 main(VSOutput input) : SV_TARGET
{
    // 1. 基本UVと深度の計算
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV.x = screenUV.x * 0.5f + 0.5f;
    screenUV.y = -screenUV.y * 0.5f + 0.5f;

    float bgDepthZ = depthTex.Sample(smp, screenUV).r;
    float bgLinearDepth = LinearizeDepth(bgDepthZ);
    float waterLinearDepth = LinearizeDepth(input.screenPos.z / input.screenPos.w);
    float depthDiff = max(bgLinearDepth - waterLinearDepth, 0.0f);
    float waterDepthFactor = saturate(depthDiff / 3.0f);

// ==========================================
// 2. ノイズによる波面の生成 (流れの実装)
// ==========================================
// エディタで設定した flowSpeed に基づいてUVをオフセットさせる
    float2 flowOffset = float2(flowSpeedX, flowSpeedY) * time;
    float2 noiseUV = (input.worldPos.xz + flowOffset) * 1.5f;

// 2層のノイズを使い、さらに複雑な流れを作る
    float n1 = noise(noiseUV + float2(time * 0.2f, time * 0.1f));
    float n2 = noise(noiseUV * 1.5f - float2(time * 0.1f, time * 0.2f));
    float waveNoise = (n1 + n2) * 0.5f;

    // ==========================================
    // 3. ライティング と 水面の反射（Reflection）
    // ==========================================
    float3 lightDir = normalize(float3(1.0f, -1.0f, 1.0f));
    float3 normal = normalize(input.normal);
    float3 detailNormal = normalize(normal + float3(waveNoise * 0.15f, 0.0f, waveNoise * 0.15f));

    float diffuse = max(dot(detailNormal, -lightDir), 0.0f) * 0.5f + 0.5f;
    float3 viewDir = normalize(float3(0.0f, 0.5f, -1.0f));
    
    // 太陽のハイライト（Specular）
    float3 lightReflectDir = reflect(lightDir, detailNormal);
    float specular = pow(max(dot(viewDir, lightReflectDir), 0.0f), 60.0f) * waveNoise;
    
    // 視線の反射ベクトル（カメラから見て、波がどこを映しているか）
    float3 viewReflectDir = reflect(-viewDir, detailNormal);
    
    // 疑似的な空の色（上が濃い青、下が白っぽい水色）
    // 反射ベクトルのY方向（上を向いているか）で空の色を変える
    float skyFactor = smoothstep(0.0f, 1.0f, viewReflectDir.y);
    float3 skyColor = lerp(float3(0.7f, 0.85f, 1.0f), float3(0.1f, 0.4f, 0.8f), skyFactor);

    // フレネル（斜めから見るほど反射が強くなる）
    float fresnel = pow(1.0f - max(dot(viewDir, detailNormal), 0.0f), 3.0f);
// ==========================================
    // ★ 4. 屈折 (Refraction) と ゆらぎ (Caustics) の計算
    // ==========================================
    // 波の法線を使って背景を歪ませる
    float2 distortedUV = screenUV + detailNormal.xz * 0.05f;
    float3 refractionColor = grabTex.Sample(smp, distortedUV).rgb;

    // ------------------------------------------
    // 【追加】水底の光のゆらぎ（Caustics）
    // ------------------------------------------
    // スケールを変えた動くUVを用意
    float2 causticUV = input.worldPos.xz * 2.5f;
    
    // 2つのノイズを別々の方向にハイスピードで流す
    float c1 = noise(causticUV + float2(time * 1.2f, time * 0.8f));
    float c2 = noise(causticUV * 1.5f - float2(time * 0.5f, time * 1.1f));
    
    // 【魔法の計算】ノイズの差分を取って累乗（pow）することで、
    // ぼんやりしたノイズが「鋭い光の網目（線）」に変換されます
    float causticPattern = pow(max(1.0f - abs(c1 - c2), 0.0f), 8.0f);
    
    // 水深に応じて減衰（深海では光が届かないため消す）
    // 15.0f の数値を小さくすると、より浅い場所でしか見えなくなります
    float causticFade = 1.0f - saturate(depthDiff / 15.0f);
    
    // 屈折した水底の景色に、ゆらめく光（少し黄色っぽい白）を加算！
    refractionColor += float3(1.0f, 1.0f, 0.8f) * causticPattern * causticFade * 1.5f;

    // 5. 泡 (Foam)
    float foamMask = 1.0f - saturate(depthDiff / 0.8f);
    float foam = step(0.6f, waveNoise) * foamMask;

    // ==========================================
    // 6. 最終的な色の合成
    // ==========================================
    float4 finalColor = color;
    
    // 水際〜浅瀬は「屈折した背景色」、深海は「Inspectorで設定した水色」
    finalColor.rgb = lerp(refractionColor, finalColor.rgb, waterDepthFactor);
    finalColor.a = lerp(0.0f, 1.0f, saturate(waterDepthFactor * 4.0f));

    // 陰影（ディフューズ）と太陽のキラキラ（スペキュラ）を加算
    finalColor.rgb = (finalColor.rgb * diffuse) + (float3(1.0f, 1.0f, 1.0f) * specular * 1.5f);
    
    // ただ白くするのではなく、反射した「空の色」をフレネルで乗せる！
    finalColor.rgb = lerp(finalColor.rgb, skyColor, fresnel * 0.8f);
    
    // 泡（Foam）を足す
    finalColor.rgb += float3(1.0f, 1.0f, 1.0f) * foam;
    
    finalColor.a = min(finalColor.a + fresnel * 0.5f + foam, 1.0f);

    return finalColor;
}