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
    int useTextureColor;
    float chantFlowStrength;
    float chantRibbonStrength;
    float ringEnergyStrength;
    float ringSpinSpeed;
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
    // Reveal mode 1 wipes horizontally; mode 2 draws a clockwise arc from the top.
    if (enableReveal == 1 && input.uv.x > revealProgress)
        discard;
    if (enableReveal == 2)
    {
        float2 centeredUv = input.uv - float2(0.5f, 0.5f);
        float sweep = atan2(centeredUv.x, -centeredUv.y) / 6.2831853f;
        if (sweep < 0.0f)
            sweep += 1.0f;
        if (sweep > saturate(revealProgress))
            discard;
    }
        
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
    float alphaMask = mainColor.a * baseNoiseValue;
    float distMask = mainColor.a;
    
    if (proceduralType == 0)
    {
        // Type 0: 通常のテクスチャ
        // (デフォルト初期値を使用)
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

        // Color opt-in also preserves authored alpha on flowing ribbons.
        float authoredAlpha = useTextureColor != 0 ? mainColor.a : 1.0f;
        alphaMask = sphere * baseNoiseValue * authoredAlpha;
        distMask = sphere * authoredAlpha;
        mainColor.r = 1.0f;
    }
    else if (proceduralType == 3)
    {
        // Type 3: プロシージャル「モヤモヤノイズ（広範囲オーラ）」
        alphaMask = baseNoiseValue;
        distMask = baseNoiseValue;
        mainColor.r = 1.0f;
    }
    else if (proceduralType == 7)
    {
        // Plane sprites should keep the authored texture alpha.
        alphaMask = mainColor.a;
        distMask = mainColor.a;
        // Legacy grayscale tint keeps its alpha-derived brightness.
        if (useTextureColor == 0)
            mainColor.r = max(mainColor.r, mainColor.a);
    }

    if (proceduralType == 13 || proceduralType == 14)
    {
        // Keep the field anchored in world space. Only the bolt topology and
        // travelling energy packets change; the surface never slides as a decal.
        float phase = time * max(abs(ringSpinSpeed), 1.0f);
        float epoch = floor(phase);
        float core = 0.0f;
        float halo = 0.0f;
        float density = saturate(ringEnergyStrength);
        if (proceduralType == 13)
        {
            float2 grid = input.uv * 9.0f;
            float2 cell = floor(grid);
            float2 local = frac(grid);
            float seed = hash(cell + 19.0f);
            float activity = smoothstep(1.0f - density, 1.06f - density,
                hash(cell + epoch * 0.17f));
            [unroll] for (int branch = 0; branch < 3; ++branch)
            {
                float2 q = branch == 1 ? local.yx : local;
                float segment = q.x * 7.0f;
                float left = floor(segment);
                float y0 = hash(cell + float2(left + branch * 23.0f, epoch)) * 0.72f + 0.14f;
                float y1 = hash(cell + float2(left + 1.0f + branch * 23.0f, epoch)) * 0.72f + 0.14f;
                float bolt = abs(q.y - lerp(y0, y1, frac(segment)));
                float packet = frac(q.x - time * (1.7f + seed) + seed + branch * 0.31f);
                float travel = lerp(0.42f, 1.0f, 1.0f - smoothstep(0.04f, 0.38f, packet));
                float flicker = 0.55f + 0.45f * hash(cell + float2(floor(phase * 2.0f), branch));
                float energy = activity * flicker * lerp(1.0f, travel, chantFlowStrength);
                float aa = max(fwidth(bolt), 0.003f);
                core = max(core, (1.0f - smoothstep(0.006f, 0.014f + aa, bolt)) * energy);
                halo = max(halo, exp(-bolt * 27.0f) * energy);
            }
            float edge = 1.0f - smoothstep(0.90f, 1.0f, length((input.uv - 0.5f) * 2.0f));
            core *= edge;
            halo *= edge;
        }
        else
        {
            // Layer jagged radial bolts with sharp impact rays and a soft red
            // energy volume. Billboards make the burst readable from every angle.
            float2 p = (input.uv - 0.5f) * 2.0f;
            float radius = length(p);
            float angle = atan2(p.y, p.x);
            float spoke = angle / 6.2831853f * 11.0f;
            float index = floor(spoke + 0.5f);
            float segment = radius * 9.0f;
            float j0 = hash(float2(floor(segment), index + epoch * 13.0f)) - 0.5f;
            float j1 = hash(float2(floor(segment) + 1.0f, index + epoch * 13.0f)) - 0.5f;
            float jag = lerp(j0, j1, frac(segment)) * 0.48f;
            float bolt = abs(spoke - index - jag) * max(radius, 0.12f);
            float fade = 1.0f - smoothstep(0.63f, 1.0f, radius);
            float packet = frac(radius * 0.65f - time * 2.8f + hash(float2(index, 7.0f)));
            float energy = (0.55f + 0.45f * (1.0f - smoothstep(0.0f, 0.4f, packet))) * fade;
            float aa = max(fwidth(bolt), 0.002f);
            core = (1.0f - smoothstep(0.008f, 0.024f + aa, bolt)) * energy;
            float ray = pow(saturate(1.0f - abs(spoke - index) * 6.0f), 8.0f) * fade;
            core = max(core, ray * (0.3f + 0.7f * density));
            halo = exp(-bolt * 22.0f) * fade + exp(-radius * radius * 6.0f) * 0.20f;
        }
        alphaMask = saturate(core * 0.95f + halo * 0.58f);
        distMask = alphaMask;
        mainColor.rgb = lerp(float3(1.0f, 0.012f, 0.006f), float3(1.8f, 1.15f, 0.95f), saturate(core * 1.7f));
    }

    // A polar flow mask turns a simple plane into rotating ground energy.
    // It is opt-in so existing mesh effects keep their authored appearance.
    if (chantFlowStrength > 0.001f && proceduralType == 7)
    {
        float2 p = (input.uv - float2(0.5f, 0.5f)) * 2.0f;
        float radius = length(p);
        float angle = atan2(p.y, p.x);
        float warpedRadius = radius + (baseNoiseValue - 0.5f) * 0.055f;
        float angularSpeed = 2.3f + abs(scrollSpeed.x) * 2.0f;
        float radialSpeed = 1.5f + abs(scrollSpeed.y);
        float phase = angle * 3.0f - warpedRadius * 13.0f + time * angularSpeed;
        float counterPhase = -angle * 5.0f - warpedRadius * 19.0f - time * radialSpeed;
        float innerFade = smoothstep(0.12f, 0.28f, radius);
        float outerFade = 1.0f - smoothstep(0.83f, 1.0f, radius);
        float filament = pow(saturate(0.5f + 0.5f * sin(phase)), 12.0f) * innerFade * outerFade;
        float secondFilament = pow(saturate(0.5f + 0.5f * sin(counterPhase)), 18.0f) *
            smoothstep(0.34f, 0.55f, radius) * outerFade;
        float rimDistance = (warpedRadius - 0.76f) * 13.0f;
        float rim = exp(-rimDistance * rimDistance) *
            (0.72f + 0.28f * sin(angle * 6.0f - time * 2.0f));
        float centerGlow = exp(-radius * radius * 14.0f) * 0.25f;
        // Keep a faint floor wash so the moving filaments carry the bright accents.
        float washBand = smoothstep(0.32f, 0.49f, radius) *
            (1.0f - smoothstep(0.82f, 0.98f, radius));
        float washMotion = 0.74f + 0.26f * sin(angle * 2.0f - radius * 7.0f + time * 1.6f);
        float groundWash = washBand * washMotion * (0.72f + 0.28f * baseNoiseValue) * 0.17f;
        float flow = saturate(filament * 0.85f + secondFilament * 0.38f +
            rim * 0.72f + centerGlow + groundWash);
        alphaMask = flow * outerFade * chantFlowStrength;
        distMask = alphaMask;
        mainColor.rgb = 1.0f;
    }

    // The helical mesh carries a soft outer ribbon and a moving bright filament.
    // This mask belongs to the caster, independent of the ground sigil.
    if (chantRibbonStrength > 0.001f && proceduralType == 2)
    {
        float along = saturate(input.uv.x);
        float across = saturate(input.uv.y);
        float endFade = smoothstep(0.0f, 0.10f, along) *
            (1.0f - smoothstep(0.84f, 1.0f, along));
        float movingCenter = 0.5f + 0.10f * sin(along * 17.0f - time * (4.0f + abs(scrollSpeed.x)));
        float distanceFromCore = abs(across - movingCenter);
        float softBody = exp(-distanceFromCore * distanceFromCore * 14.0f);
        float brightCore = exp(-distanceFromCore * distanceFromCore * 85.0f);
        // Moving windows break the continuous helix into short flowing wisps.
        float travelingLight = 0.16f + 0.84f * pow(saturate(
            0.5f + 0.5f * sin(along * 16.0f - time * 5.0f)), 2.5f);
        float turbulence = 0.66f + 0.34f * baseNoiseValue;
        alphaMask = saturate((softBody * 0.24f + brightCore * 0.76f) *
            endFade * travelingLight * turbulence * chantRibbonStrength);
        distMask = alphaMask;
        mainColor.rgb = float3(0.68f + brightCore * 0.38f,
            0.68f + brightCore * 0.38f, 0.68f + brightCore * 0.38f);
    }

    // A ground ribbon joins the three fire anchors without a stretched sprite.
    // The thin traveling core reads as a drawn spell boundary during the wipe.
    if (chantRibbonStrength > 0.001f && proceduralType == 7)
    {
        float across = abs(input.uv.y - 0.5f) * 2.0f;
        float sideGlow = exp(-across * across * 13.0f);
        float hotCore = exp(-across * across * 115.0f);
        float ends = smoothstep(0.0f, 0.035f, input.uv.x) *
            (1.0f - smoothstep(0.965f, 1.0f, input.uv.x));
        float travel = pow(saturate(0.5f + 0.5f *
            sin(input.uv.x * 29.0f - time * 7.0f)), 5.0f);
        float glimmer = 0.74f + 0.26f * baseNoiseValue;
        alphaMask = saturate((sideGlow * 0.24f + hotCore * (0.48f + travel * 0.28f)) *
            ends * glimmer * chantRibbonStrength);
        distMask = alphaMask;
        mainColor.rgb = float3(1.0f, 0.84f + hotCore * 0.16f, 0.70f + hotCore * 0.30f);
    }

    // Opt-in torus energy: moving teeth, counterflow and concentrated hot spots.
    // The faint body keeps the ring readable while the highlights imply a fast cutting edge.
    if (ringEnergyStrength > 0.001f && proceduralType == 8)
    {
        float angle = frac(input.uv.x) * 6.2831853f;
        float spin = time * ringSpinSpeed;
        float teeth = pow(saturate(0.5f + 0.5f * sin(angle * 12.0f - spin * 5.0f)), 9.0f);
        float counterflow = pow(saturate(0.5f + 0.5f * sin(angle * 7.0f + spin * 2.4f)), 12.0f);
        float hotSpot = pow(saturate(0.5f + 0.5f * sin((angle - spin) * 3.0f)), 20.0f);
        float grain = valueNoise(float2(input.uv.x * 64.0f - time * 2.4f,
            input.uv.y * 5.0f + time * 1.8f));
        float chargedBody = 0.14f + 0.18f * grain;
        float cuttingEdge = teeth * 0.42f + counterflow * 0.16f + hotSpot * 0.72f;
        alphaMask = saturate((chargedBody + cuttingEdge) * ringEnergyStrength);
        distMask = alphaMask;
        float heat = saturate(hotSpot * 0.94f + teeth * 0.40f);
        mainColor.rgb = lerp(float3(0.36f, 0.16f, 0.70f),
            float3(1.35f, 1.22f, 1.30f), heat);
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
    // Opt-in preserves the authored RGB of colorful plane textures.
    float3 textureColor = useTextureColor != 0 ? mainColor.rgb : mainColor.rrr;
    float3 tintColor = baseColor * intensity * textureColor;
    
    float4 glowColor = float4(tintColor, alphaMask * color.a);

    // A narrow hot edge makes the dissolving lance read as energy breaking apart.
    if (proceduralType == 12 && dissolveFade > 0.001f)
    {
        float edgeDistance = baseNoiseValue - dissolveFade;
        float edge = (1.0f - smoothstep(0.0f, 0.12f, edgeDistance)) *
            saturate(dissolveFade * 5.0f) * (1.0f - saturate(dissolveFade * 0.70f));
        glowColor.rgb += edge * float3(2.0f, 0.48f, 2.3f);
        glowColor.a = max(glowColor.a, edge * color.a * 0.72f);
    }

    // ========================================================
    // 6. 最終出力
    // ========================================================
    // alphaReferenceで設定した閾値以下のピクセルを破棄 (歪み・通常共通)
    if (glowColor.a <= alphaReference)
        discard;

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

        // 歪み描画時は背景を自前で描いているため、アルファは強制 1.0 で出力
        return float4(finalRGB, 1.0f);
    }
    else
    {
        return glowColor;
    }
}
