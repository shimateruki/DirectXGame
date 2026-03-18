// ==========================================
// LocalFog.PS.hlsl (究極のボリューメトリック・フォグ)
// ==========================================

struct VSOutput
{
    float4 svpos : SV_POSITION;
    float4 projPos : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 localPos : TEXCOORD2;
};

Texture2D<float> depthTex : register(t0);
SamplerState smp : register(s0);

// C++側でALLにしたことで、ピクセルシェーダーでも箱の行列が読める！
cbuffer Transform : register(b0)
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

cbuffer LocalFogData : register(b1)
{
    float4 fogColor;
    float3 cameraPos;
    float fogDensity;
    float4x4 inverseViewProj;
    float time;
    float edgeFade;
    float noiseSpeed;
    float noiseScale;
    float3 lightDirection;
    float scatteringIntensity;
    float3 lightColor;
    float scatteringG;
};

float hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise3D(float3 x)
{
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash(p + float3(0, 0, 0)), hash(p + float3(1, 0, 0)), f.x),
                     lerp(hash(p + float3(0, 1, 0)), hash(p + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(hash(p + float3(0, 0, 1)), hash(p + float3(1, 0, 1)), f.x),
                     lerp(hash(p + float3(0, 1, 1)), hash(p + float3(1, 1, 1)), f.x), f.y), f.z);
}

float4 main(VSOutput input) : SV_TARGET
{
    // 1. 画面のUVと背景のデプスを取得
    float2 ndc = input.projPos.xy / input.projPos.w;
    float2 screenUV = ndc * 0.5f + 0.5f;
    screenUV.y = 1.0f - screenUV.y;
    float bgDepth = depthTex.Sample(smp, screenUV);

    // 2. 背景のワールド座標を復元
    float4 bgClipPos = float4(ndc.x, ndc.y, bgDepth, 1.0f);
    float4 bgWorldPosW = mul(bgClipPos, inverseViewProj);
    float3 bgWorldPos = bgWorldPosW.xyz / bgWorldPosW.w;

    // 3. ローカル空間（箱の空間）に変換して、視線（レイ）を計算
    float4x4 WorldInv = transpose(WorldInverseTranspose); // 逆行列の復元
    float3 localCamPos = mul(float4(cameraPos, 1.0f), WorldInv).xyz;
    float3 localBgPos = mul(float4(bgWorldPos, 1.0f), WorldInv).xyz;
    
    float3 rayDirLocal = localBgPos - localCamPos;
    float localBgDist = length(rayDirLocal);
    float3 rayDirLocalNorm = rayDirLocal / localBgDist;

    // 4. 箱（-0.5 ～ 0.5）との交差判定 (Ray-AABB アルゴリズム)
    float3 invDir = 1.0f / rayDirLocalNorm;
    float3 t0 = (-0.5f - localCamPos) * invDir;
    float3 t1 = (0.5f - localCamPos) * invDir;
    float3 tMin = min(t0, t1);
    float3 tMax = max(t0, t1);
    
    float tEnter = max(max(tMin.x, tMin.y), tMin.z);
    float tExit = min(min(tMax.x, tMax.y), tMax.z);

    // 交差していない場合は描画しない
    if (tEnter > tExit || tExit < 0.0f)
    {
        return float4(0, 0, 0, 0);
    }

    // 5. 「箱の中」を貫通したレイの長さを計算
    float startDist = max(tEnter, 0.0f);
    float endDist = min(tExit, localBgDist);
    float localThickness = endDist - startDist;

    // 背景が箱より手前にある場合は描画しない
    if (localThickness <= 0.0f)
    {
        return float4(0, 0, 0, 0);
    }

    // 6. ローカル座標をワールド空間に戻して「本当の厚み」を計算
    float3 startLocalPos = localCamPos + rayDirLocalNorm * startDist;
    float3 endLocalPos = localCamPos + rayDirLocalNorm * endDist;
    float3 startWorldPos = mul(float4(startLocalPos, 1.0f), World).xyz;
    float3 endWorldPos = mul(float4(endLocalPos, 1.0f), World).xyz;
    
    float thickness = distance(startWorldPos, endWorldPos);
    
    // （もし背景が空なら、箱の裏面までの厚みでカットする）
    if (bgDepth >= 1.0f)
    {
        thickness = distance(startWorldPos, mul(float4(localCamPos + rayDirLocalNorm * tExit, 1.0f), World).xyz);
    }

    // 7. 霧の基本濃度 (箱の形に囚われない正確な厚み！)
    float fogAmount = 1.0f - exp(-thickness * fogDensity);

    // 8. ★修正版エッジフェード★
    // 箱の中の「中間地点」を使って境界線をボカす！
    float3 midLocalPos = (startLocalPos + endLocalPos) * 0.5f;
    float3 absMid = abs(midLocalPos);
    float maxDist = max(max(absMid.x, absMid.y), absMid.z);
    
    float edge = saturate((0.5f - maxDist) / max(edgeFade, 0.001f));
    edge = smoothstep(0.0f, 1.0f, edge);
    fogAmount *= edge;

// =======================================================
    // 9. ノイズでモヤモヤさせる (フワッとした霧への最適化)
    // =======================================================
    float3 midWorldPos = (startWorldPos + endWorldPos) * 0.5f;

    // --- ノイズ1（ベースのゆっくりした大きなモヤ） ---
    float3 pos1 = midWorldPos * noiseScale;
    pos1.x += time * noiseSpeed;
    pos1.z += time * (noiseSpeed * 0.3f);
    float n1 = noise3D(pos1);

    // --- ノイズ2（少し速く流れる細かいディテール） ---
    // ※スケールを2倍にして細かくし、少し逆向きに流す
    float3 pos2 = midWorldPos * (noiseScale * 2.0f);
    pos2.x -= time * (noiseSpeed * 1.2f);
    pos2.z -= time * (noiseSpeed * 0.5f);
    float n2 = noise3D(pos2);

    // ★修正1：掛け算(流体)をやめて、純粋な足し算(FBM:フラクタルノイズ)にする！
    // 60%の大きなモヤと、40%の細かいモヤを足し合わせる
    float n = n1 * 0.6f + n2 * 0.4f;
    
    // ★追加2：上に行くほどスッと消える「高さフェード (Height Falloff)」
    // ローカル座標のYは -0.5(底) ～ 0.5(天井) なので、天井付近でアルファを削る
    float heightFade = saturate((0.5f - midLocalPos.y) / max(edgeFade, 0.001f));
    heightFade = smoothstep(0.0f, 1.0f, heightFade);

    // 最終的なノイズ係数 (0.1 ~ 1.0 の間で揺らす)
    float noiseFactor = lerp(0.1f, 1.0f, n);
    
    // 全てを掛け合わせる
    fogAmount *= noiseFactor * heightFade;
    
// 視線の向き
    float3 viewDirWorld = normalize(endWorldPos - cameraPos);
    
    // 視線と太陽光の向きの内積
    float sunDot = dot(viewDirWorld, -lightDirection);
    
    // ★修正: エディタから送られてきた変数を使う
    float g = scatteringG;
    
    // Henyey-Greenstein 位相関数
    float phase = (1.0f - g * g) / pow(abs(1.0f + g * g - 2.0f * g * sunDot), 1.5f);
    
    // ★修正: エディタから送られてきた変数(scatteringIntensity)を使う
    float3 finalColor = fogColor.rgb + (lightColor * phase * scatteringIntensity);

    // 最終出力
    return float4(finalColor, fogColor.a * fogAmount);

}