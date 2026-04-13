#include "Water.hlsli"

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

float4 main(VSOutput input) : SV_TARGET
{
    // =========================================================
    // ★魔法のコード: モデルの滑らかな法線を無視して、
    // カメラから見た「真の真っ平らな法線」を計算する
    // =========================================================
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 flatNormal = abs(normalize(cross(dx, dy)));

    float2 baseUV;
    
    // 真の平らな法線を使って、どの面かを完璧に（100%の精度で）判定する
    if (flatNormal.y > 0.5f)
    {
        // 上面・底面 (XZ座標で、初代の完璧なまだら模様を出す！)
        baseUV = input.worldPos.xz * 0.4f + float2(uvOffsetX, uvOffsetY);
    }
    else if (flatNormal.x > 0.5f)
    {
        // 側面X (ZY座標で、重力に合わせて下へ流れる)
        baseUV = input.worldPos.zy * 0.4f + float2(uvOffsetY, -time * 0.05f);
    }
    else
    {
        // 側面Z (XY座標で、重力に合わせて下へ流れる)
        baseUV = input.worldPos.xy * 0.4f + float2(uvOffsetX, -time * 0.05f);
    }

    // =========================================================
    // 初代の完璧なノイズ計算 (if文で1方向だけ計算するので処理も軽い！)
    // =========================================================
    float n1 = noise(baseUV + float2(time * 0.02f, time * 0.01f));
    float n2 = noise(baseUV * 1.8f - float2(time * 0.01f, time * 0.03f));
    float magmaNoise = (n1 + n2) * 0.5f;

    // 初代の完璧な色マッピング
    float3 darkCrust = color.rgb * 0.1f;
    float3 redLava = color.rgb;
    float3 yellowHot = float3(1.5f, 1.2f, 0.0f);

    float3 finalColor = darkCrust;
    finalColor = lerp(finalColor, redLava, smoothstep(0.4f, 0.6f, magmaNoise));
    finalColor = lerp(finalColor, yellowHot, smoothstep(0.7f, 0.8f, magmaNoise));

    return float4(finalColor, 1.0f);
}