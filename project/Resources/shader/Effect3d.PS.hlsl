// ========================================================
// Effect3d.PS.hlsl - メッシュエフェクト用ピクセルシェーダー（完全版）
// ========================================================

Texture2D<float4> mainTex : register(t0); // 斬撃の画像
Texture2D<float4> grabTex : register(t1); // 背景コピー（歪み用）
Texture2D<float4> noiseTex : register(t2); // ノイズ画像（ディゾルブ用）
Texture2D<float4> rampTex : register(t3);
SamplerState smp : register(s0);

// C++側の EffectObject3d::EffectMaterial と一致
cbuffer EffectMaterial : register(b0)
{
    float4 color; // エフェクトの基本色
    float2 scrollSpeed; // UVスクロール速度
    float time; // 経過時間
    float intensity; // 発光強度
    float dissolveFade; // 消滅のしきい値
    float revealProgress; // 出現の進行度
    float distortionStrength; // 歪みの強さ
    float distortionSpeed; // 歪みの速さ（人力ノイズ用）
    float edgeFadeStrength; // 刃の縁の削れ具合 (powの指数)
    float alphaReference; // ★discardの閾値 (0以外でdiscardしたい値を設定)
    float2 screenSize; // 画面サイズ
    int enableDistortion; // 歪み有効フラグ (0:OFF, 1:ON)
    int enableColorRamp; // カラーランプ有効フラグ
    int enableNoiseTexture; // ノイズテクスチャ有効フラグ
    int enableReveal;
    int proceduralType; 
    float3 padding2;
};
// --- プロシージャル計算用関数 ---
// 2Dランダムハッシュ
float hash(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}
// シンプルなバリューノイズ
float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash(i + float2(0.0, 0.0)), hash(i + float2(1.0, 0.0)), u.x),
                lerp(hash(i + float2(0.0, 1.0)), hash(i + float2(1.0, 1.0)), u.x), u.y);
}
struct VertexOutput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float4 clipPos : TEXCOORD1;
};

// ========================================================
// 人力ノイズ（プロシージャルノイズ）生成関数
// ========================================================
float random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453123);
}

float noise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    float a = random(i);
    float b = random(i + float2(1.0, 0.0));
    float c = random(i + float2(0.0, 1.0));
    float d = random(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}
// ③ 軽量版FBM (3回重ね)
// 命令数を抑えつつ、複雑な揺らぎを作ります
float fbm_light(float2 uv)
{
    float v = 0.0;
    float a = 0.5;
    // 3回ループなら制限に余裕で収まります
    for (int i = 0; i < 3; ++i)
    {
        v += a * noise(uv);
        uv = uv * 2.0 + 100.0; // 行列計算を省いて高速化
        a *= 0.5;
    }
    return v;
}
float4 main(VertexOutput input) : SV_TARGET
{
    // 1. エディタからの制御で進行方向に合わせて消す (Reveal)
    if (enableReveal == 1 && input.uv.x > revealProgress)
        discard;
        
    float2 scrolledUV = input.uv + (scrollSpeed * time);
    float4 mainColor = mainTex.Sample(smp, scrolledUV);

    // ========================================================
    // 2. ベースとなるノイズ値を取得 (テクスチャ or FBM)
    // ========================================================
    float baseNoiseValue = 0.0f;
    float2 distOffset = float2(0.0f, 0.0f);

    if (enableNoiseTexture == 1)
    {
        // 画像がある場合は、画像のR(赤)とG(緑)成分を使う
        float4 nTex = noiseTex.Sample(smp, scrolledUV);
        baseNoiseValue = nTex.r;
        distOffset = (nTex.rg - 0.5f) * 2.0f; // 歪み方向
    }
    else
    {
        // 画像がない(None)場合は、シェーダーの自己計算(FBM)を使う
        float nScale = 4.0f;
        float n1 = fbm_light(scrolledUV * nScale);
        float n2 = fbm_light(scrolledUV * nScale + 1.0f);
        baseNoiseValue = n1;
        distOffset = (float2(n1, n2) - 0.5f) * 2.0f;
    }

    // 3. ディゾルブ消滅
    if (baseNoiseValue < dissolveFade)
        discard;
// 4. マスク計算 (★プロシージャルTypeで形を分岐！)
    // ========================================================
    float alphaMask = 1.0f;
    float distMask = 1.0f;
    
    if (proceduralType == 0)
    {
        // Type 0: 通常のテクスチャ
        alphaMask = mainColor.a * baseNoiseValue;
        distMask = mainColor.a;
    }
    else if (proceduralType == 1)
    {
        // Type 1: プロシージャル「鋭い斬撃 (SAO風)」ガチ版
        
        // ① 尻尾に向かって細くなる「えぐり」の計算
        float trail = 1.0f - saturate(input.uv.x); // 先端(1.0) -> 尻尾(0.0)
        float thickness = pow(trail, 0.5f); // 尻尾に向かって急激に細くなるカーブ
        
        // ② Y軸の中心(0.5)から端への距離を計算し、刃の鋭さを作る
        float yDist = abs(input.uv.y - 0.5f) * 2.0f;
        float widthMask = 1.0f - (yDist / max(thickness, 0.001f)); // 0除算防止
        
        // edgeFadeStrengthで刃先のシャープさを極限まで高める
        widthMask = saturate(pow(max(widthMask, 0.0f), abs(edgeFadeStrength) + 1.0f));
        float trailMask = smoothstep(0.0f, 1.0f, trail); // 尻尾をスッとフェードアウト

        float2 streakUV = float2(input.uv.x * 2.0f - (time * 8.0f), input.uv.y * 5.0f);
        float streakNoise = smoothstep(0.1f, 0.9f, fbm_light(streakUV));

        // 全部掛け合わせる！
        float baseShape = widthMask * trailMask;
        float energyMask = (streakNoise * 0.7f) + 0.3f;
        
        alphaMask = baseShape * energyMask * baseNoiseValue;
        distMask = baseShape;
        mainColor.r = 1.0f;
    }
    else if (proceduralType == 2)
    {
        // Type 2: プロシージャル「オーラ・球体」
        // ★修正: smoothstep(0, 0.5, dist) の形にしてから 1.0 から引く！
        float dist = distance(input.uv, float2(0.5f, 0.5f));
        float sphere = 1.0f - smoothstep(0.0f, 0.5f, dist);
        
        alphaMask = sphere * baseNoiseValue;
        distMask = sphere;
        mainColor.r = 1.0f;
    }
    else if (proceduralType == 3)
    {
        // Type 3: プロシージャル「モヤモヤノイズ（広範囲オーラ）」
        alphaMask = baseNoiseValue;
        distMask = baseNoiseValue;
        mainColor.r = 1.0f;
    }

    // ========================================================
    // 5. 発光カラーの計算 (カラーランプ切り替え)
    // ========================================================
    float3 baseColor = color.rgb;
    if (enableColorRamp == 1)
    {
        float rampU = saturate(input.uv.y);
        baseColor = rampTex.Sample(smp, float2(rampU, 0.5f)).rgb;
    }
    
    // mainColor.rを掛けることで、テクスチャの濃淡(白黒)も反映させる
    float3 tintColor = baseColor * intensity * mainColor.r;
    
    float4 glowColor = float4(tintColor, alphaMask * color.a);

    // ========================================================
    // 6. 最終出力
    // ========================================================
    if (enableDistortion == 1)
    {
        float2 ndc = input.clipPos.xy / input.clipPos.w;
        float2 screenUV = ndc * float2(0.5f, -0.5f) + 0.5f;

        // distOffset(テクスチャの歪み) と distMask(斬撃の形) を掛け合わせる
        float2 offset = distOffset * distortionStrength * distMask * 0.01f;

        // 背景を切り抜く
        float3 distortedBg = grabTex.Sample(smp, screenUV + offset).rgb;
        
        // ★ max(0.3)を完全に削除！
        // 歪んだ背景の上に、純粋に「発光色(glowColor.rgb)」を足し合わせる！
        float3 finalRGB = distortedBg + glowColor.rgb;

        // 歪み描画時は背景を自前で描いているため、アルファは強制1.0でOK
        return float4(finalRGB, 1.0f);
    }
    else
    {
        // alphaReferenceで設定した閾値以下のピクセルを破棄
        // (0.0を指定すると完全透明のみdiscardする挙動になる)
        if (glowColor.a <= alphaReference)
            discard;
        return glowColor;
    }
}