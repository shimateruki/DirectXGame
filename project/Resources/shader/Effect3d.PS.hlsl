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
    float padding1;
    float2 screenSize; // 画面サイズ
    int enableDistortion; // 歪み有効フラグ (0:OFF, 1:ON)
    int enableColorRamp; // ★追加
    int enableNoiseTexture; // ★追加
    float padding2; // ★追加
};

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
    if (input.uv.x > revealProgress)
        discard;

    float2 scrolledUV = input.uv + (scrollSpeed * time);
    float4 mainColor = mainTex.Sample(smp, scrolledUV);

    // ========================================================
    // ★ ベースとなるノイズ値を取得 (テクスチャ or FBM)
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

// --------------------------------------------------------
    // 4. マスク計算 (★ここを「斬撃の形」に書き換える！)
    // --------------------------------------------------------
    
    // ① 尻尾のフェード（X軸：弧の長さ）
    // 先端(0.0)は濃く、尻尾(1.0)に向かってスッと消えるグラデーション
    float trailMask = smoothstep(1.0f, 0.0f, input.uv.x);
    // もし向きが逆（尻尾が太くなる）なら、 smoothstep(0.0f, 1.0f, input.uv.x) に変えてください！

    // ② 刃の鋭さ（Y軸：太さ）
    // sin波を使って「中心が1.0、両端が0.0」になる丸みを作り、それを累乗して鋭く尖らせる
    float widthMask = sin(saturate(input.uv.y) * 3.14159f);
    widthMask = pow(widthMask, abs(edgeFadeStrength) + 1.0f); // edgeFadeStrengthで鋭さを調整

    // ③ 合成
    // 読み込んだノイズの形 × 尻尾フェード × 刃の鋭さ
    float alphaMask = baseNoiseValue * trailMask * widthMask;
    
    // 歪み用のマスク（縁が四角く歪むのを防ぐ）
    float distMask = widthMask;

    // 5. 発光カラーの計算 (カラーランプ切り替え)
    float3 baseColor = color.rgb;
    if (enableColorRamp == 1)
    {
        float rampU = saturate(input.uv.y);
        baseColor = rampTex.Sample(smp, float2(rampU, 0.5f)).rgb;
    }
    float3 tintColor = baseColor * intensity * mainColor.r;
    float4 glowColor = float4(tintColor, alphaMask * color.a);

    // 6. 最終出力
    if (enableDistortion == 1)
    {
        float2 ndc = input.clipPos.xy / input.clipPos.w;
        float2 screenUV = ndc * float2(0.5f, -0.5f) + 0.5f;

        // ★ 計算済みの distOffset を使う
        float2 offset = distOffset * distortionStrength * distMask * 0.01f;

        float3 distortedBg = grabTex.Sample(smp, screenUV + offset).rgb;
        float3 finalRGB = lerp(distortedBg, distortedBg + glowColor.rgb, max(alphaMask, 0.3f));

        return float4(finalRGB, 1.0f);
    }
    else
    {
        if (glowColor.a <= 0.0f)
            discard;
        return glowColor;
    }
}