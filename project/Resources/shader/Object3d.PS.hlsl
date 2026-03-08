#include "Object3d.hlsli"

static const int kMaxPointLights = 100;
static const int kMaxSpotLights = 100;

struct PixelShanderOutput
{
    float32_t4 color : SV_TARGET0;
};

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t3 padding1;
    float32_t4x4 uvTransform;
    int32_t selectedLighting;
    float32_t shininess;
    int32_t materialType;
    float32_t roughness;
    float32_t metallic;
    
    int32_t enableNormalMap;
    float padding2;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intenssity;
    float32_t3 ambientColor;
    float fogStart;
    float fogEnd;
    float32_t3 fogColor;
    int32_t enableEnvMap;
    float envIntensity;
    float2 padding2;
};

struct Camera
{
    float32_t3 worldPosition;
};

struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float radius;
    float decay;
    float32_t2 padding;
};

struct PointLightConstData
{
    PointLight lights[kMaxPointLights];
    int activeCount;
    float32_t3 padding;
};

struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float32_t3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float32_t padding;
};

struct SpotLightConstData
{
    SpotLight lights[kMaxSpotLights];
    int activeCount;
    float32_t3 padding;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLightConstData> gPointLights : register(b3);
ConstantBuffer<SpotLightConstData> gSpotLights : register(b4);

Texture2D<float32_t4> gTexture : register(t0);
TextureCube<float32_t4> gEnvTexture : register(t2);
SamplerState gSampler : register(s0);
Texture2D<float32_t4> gNormalMap : register(t3);
Texture2D<float32_t4> gOrmMap : register(t4);
Texture2D<float32_t> gShadowMap : register(t5);
SamplerState gShadowSampler : register(s1);

static const float PI = 3.14159265359;

// 1. GGX (微小面分布関数：ザラザラ具合)
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    return num / max(denom, 0.0000001f); // 0除算防止
}

// 2. Geometry (幾何減衰関数：ミクロな影)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    float num = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    return num / max(denom, 0.0000001f);
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// 3. Fresnel (フレネル反射：角度による反射率)
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

// ★あらゆるライト（平行・点・スポット）のPBR計算を統一する関数
float3 CalcPBRLight(float3 L, float3 V, float3 N, float3 radiance, float3 albedo, float roughness, float metallic, float3 F0)
{
    float3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.0001f;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= 1.0f - metallic;

    float NdotL = max(dot(N, L), 0.0f);
    // (拡散反射 + 鏡面反射) * ライトの強さ * 角度
    return (kD * albedo / PI + specular) * radiance * NdotL;
}


PixelShanderOutput main(VecrtexShaderOutput input)
{
    PixelShanderOutput output;
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // アルファテスト (ガラスの場合は透明部分も描画したいので、materialTypeが0の時だけdiscardする手もあるが、一旦そのまま)
    if (gMaterial.materialType == 0 && textureColor.a <= 0.5)
    {
        discard;
    }

    
    float shadowFactor = 1.0f; // 1.0なら日向、暗くするなら0.5などに下げる

    // W除算 (同次座標系からデカルト座標系へ)
    float3 shadowPos = input.shadowPosition.xyz / input.shadowPosition.w;

    // クリップ空間(-1 ～ 1) を UV空間(0 ～ 1) に変換
    float2 shadowUV = float2(
        (shadowPos.x + 1.0f) / 2.0f,
        (1.0f - shadowPos.y) / 2.0f // Yは上下反転
    );

    // 画面外を真っ黒にしないための範囲チェック
    if (shadowPos.z > 0.0f && shadowPos.z < 1.0f &&
        shadowUV.x > 0.0f && shadowUV.x < 1.0f &&
        shadowUV.y > 0.0f && shadowUV.y < 1.0f)
    {
        // シャドウマップから「ライトから一番近い深度」を取得
        float depthFromLight = gShadowMap.Sample(gShadowSampler, shadowUV);

        // シャドウアクネ（縞模様のノイズ）を防ぐための微小なバイアス
        float bias = 0.005f;

        // 今のピクセルの深度 が シャドウマップの深度 より遠ければ、そこは影！
        if (shadowPos.z - bias > depthFromLight)
        {
            shadowFactor = 0.0f; // 影の濃さ (0.0だと真っ黒、1.0だと影なし)
        }
    }
    
    
    float NdotL;
    float cos;
    
    switch (gMaterial.selectedLighting)
    {
        case 0: // None
            output.color = gMaterial.color * textureColor;
            break;

        case 1: // Lambert (Directional Only)
            cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intenssity;
            output.color.a = gMaterial.color.a * textureColor.a;
            break;

        case 2: // PBR (Cook-Torrance BRDF)
        {   // ★修正: case 2 の開始を波括弧で囲む！
                float3 N = normalize(input.normal);
            // ★修正: toEye ではなく V として定義し、ガラスでも使う
                float3 V = normalize(gCamera.worldPosition - input.worldPosition);

          
            // ===========================================================
            // 進化版：環境マップ対応 ガラスシェーダー (Crystal Glass Shader)
            // ===========================================================
                if (gMaterial.materialType == 1)
                {
                    float NdotV = saturate(dot(N, V));

                    // 1. プリズム屈折 (RGBごとに少しずつ角度をずらして色収差を出す)
                    float iorRatio = 1.0f / 1.52f;
                    float3 IOR_RGB = float3(iorRatio * 0.99f, iorRatio, iorRatio * 1.01f);

                    float3 RefractR = refract(-V, N, IOR_RGB.r);
                    float3 RefractG = refract(-V, N, IOR_RGB.g);
                    float3 RefractB = refract(-V, N, IOR_RGB.b);

                    // ★大進化: 偽物の色ではなく、本物の環境マップを「屈折した方向」でサンプリング！
                    float3 envR = gEnvTexture.SampleLevel(gSampler, RefractR, 0.0f).rgb;
                    float3 envG = gEnvTexture.SampleLevel(gSampler, RefractG, 0.0f).rgb;
                    float3 envB = gEnvTexture.SampleLevel(gSampler, RefractB, 0.0f).rgb;

                    // RGBを合体させて、色収差（虹色のにじみ）のある屈折色を作る
                    float3 refractionColor = float3(envR.r, envG.g, envB.b) * gDirectionalLight.envIntensity;

                    // 2. フレネル & ダークリム
                    float F0_glass = 0.04f;
                    float fresnel = F0_glass + (1.0f - F0_glass) * pow(1.0f - NdotV, 5.0f);
                    float darkRim = smoothstep(0.6f, 1.0f, 1.0f - pow(NdotV, 0.5f));

                    // ★大進化: 偽物の反射ではなく、本物の環境マップを「反射した方向」でサンプリング！
                    float3 ReflectVec = reflect(-V, N);
                    float3 reflectionColor = gEnvTexture.SampleLevel(gSampler, ReflectVec, 0.0f).rgb * gDirectionalLight.envIntensity;

                    // 3. ダブル・スペキュラ (太陽光の鋭いハイライト)
                    float3 L_Dir = normalize(-gDirectionalLight.direction);
                    float3 H_glass = normalize(L_Dir + V);
                    float NdotH_glass = saturate(dot(N, H_glass));

                    float specPowerPrimary = 8192.0f;
                    float3 specPrimary = float3(1.0f, 1.0f, 1.0f) * pow(NdotH_glass, specPowerPrimary) * 5.0f;

                    float3 N_Back = normalize(N + V * 0.2f);
                    float NdotH_Back = saturate(dot(N_Back, H_glass));
                    float specPowerSecondary = 512.0f;
                    float3 specSecondary = float3(1.0f, 1.0f, 1.0f) * pow(NdotH_Back, specPowerSecondary) * 1.0f;
                    float3 totalSpecular = specPrimary + specSecondary;

                    // 4. 集光 (Caustics)
                    float internalFocus = saturate(dot(N, -L_Dir));
                    float caustic = smoothstep(0.9f, 1.0f, internalFocus);
                    float3 fakeCaustics = float3(1.0f, 0.9f, 0.7f) * caustic * 2.0f;

                    // 5. 最終合成
                    float3 bodyColor = lerp(refractionColor * (1.0f - darkRim * 0.8f), reflectionColor, fresnel);
                    output.color.rgb = bodyColor + totalSpecular + fakeCaustics;

                    float alphaBase = 0.02f;
                    output.color.a = saturate(alphaBase + fresnel + caustic * 0.5f + (totalSpecular.r * 0.5f));
                }
                else if (gMaterial.materialType == 2)
                {
                    float NdotV = saturate(dot(N, V));

                    // 1. 氷の屈折 (IOR 1.31) とファンタジーな色収差
                    float iorRatio = 1.0f / 1.31f;
                    float3 IOR_RGB = float3(iorRatio * 0.96f, iorRatio, iorRatio * 1.04f);

                    float3 RefractR = refract(-V, N, IOR_RGB.r);
                    float3 RefractG = refract(-V, N, IOR_RGB.g);
                    float3 RefractB = refract(-V, N, IOR_RGB.b);

                    float3 envR = gEnvTexture.SampleLevel(gSampler, RefractR, 0.0f).rgb;
                    float3 envG = gEnvTexture.SampleLevel(gSampler, RefractG, 0.0f).rgb;
                    float3 envB = gEnvTexture.SampleLevel(gSampler, RefractB, 0.0f).rgb;
                    float3 refractionColor = float3(envR.r, envG.g, envB.b) * gDirectionalLight.envIntensity;

                    // 2. 氷特有の「内部の濁り」をベースカラーで表現
                    float3 iceBaseColor = gMaterial.color.rgb * textureColor.rgb;
                    // 屈折した景色に、オブジェクト自身の色を40%混ぜて濁らせる
                    refractionColor = lerp(refractionColor, iceBaseColor, 0.4f);

                    // 3. フレネル & 反射
                    float F0_ice = 0.02f;
                    float fresnel = F0_ice + (1.0f - F0_ice) * pow(1.0f - NdotV, 5.0f);
                    float3 ReflectVec = reflect(-V, N);
                    float3 reflectionColor = gEnvTexture.SampleLevel(gSampler, ReflectVec, 0.0f).rgb * gDirectionalLight.envIntensity;

                    // 4. 冷たいスペキュラ (太陽光の鋭い反射)
                    float3 L_Dir = normalize(-gDirectionalLight.direction);
                    float3 H = normalize(L_Dir + V);
                    float3 specular = float3(1.0f, 1.0f, 1.0f) * pow(saturate(dot(N, H)), 1024.0f) * 2.0f;

                    // 5. 合成
                    float3 bodyColor = lerp(refractionColor, reflectionColor, fresnel);
                    output.color.rgb = bodyColor + specular;
                    output.color.a = saturate(0.5f + fresnel + (specular.r * 0.5f)); // 氷は少し不透明度高め
                }
            // ===========================================================
            // 3. ホログラム・バリア (Hologram / Force Field)
            // ===========================================================
                else if (gMaterial.materialType == 3)
                {
                    float NdotV = saturate(dot(N, V));
                    
                    // 1. エッジ発光 (輪郭に近いほど 1.0 に近づく)
                    // powの数値を小さくする(例: 2.0)とフチが太く、大きくする(例: 5.0)と細くなります
                    float rimLight = pow(1.0f - NdotV, 3.0f);
                    
                    // 2. ベースカラー
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    
                    // 3. 発光カラーの計算 (フチは強烈に光り、正面はうっすら色を残す)
                    float3 emission = baseColor * rimLight * 3.0f; // フチを3倍の強さで光らせる
                    float3 frontColor = baseColor * 0.2f; // 正面は20%の明るさ
                    
                    output.color.rgb = emission + frontColor;
                    
                    // 4. 透明度 (フチは不透明に、正面は透明に抜く)
                    // ※エディターでブレンドモードを「加算(Add)」にするとさらに綺麗です！
                    output.color.a = saturate(rimLight * 2.0f + 0.1f) * gMaterial.color.a * textureColor.a;
                }
                else if (gMaterial.materialType == 4)
                {
                    // 1. UV座標から疑似ノイズ（ランダムな砂嵐模様）を作る
                    float2 st = input.texcoord * 15.0f; // ノイズの細かさ
                    float noise = frac(sin(dot(st, float2(12.9898f, 78.233f))) * 43758.5453123f);
                    
                    // 2. エディターの「色(Color)」の【アルファ値(A)】を「溶け具合」として流用！
                    // (A=1.0 なら無傷、Aを下げていくと徐々に溶ける)
                    float threshold = gMaterial.color.a;
                    
                    // ノイズの値がしきい値を超えたら、そのピクセルは描画せずに「穴」を開ける
                    if (noise > threshold)
                    {
                        discard;
                    }
                    
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    
                    // 3. 溶けている境界線（エッジ）をマグマのように赤熱させる
                    float edgeWidth = 0.08f; // 境界線の太さ
                    if (noise > threshold - edgeWidth)
                    {
                        float3 fireColor = float3(3.0f, 0.5f, 0.0f); // 超高輝度のオレンジ
                        output.color.rgb = baseColor + fireColor;
                    }
                    else
                    {
                        // 穴が開いていない通常部分は、シンプルな光の計算
                        float NdotL = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
                        output.color.rgb = baseColor * gDirectionalLight.color.rgb * NdotL + (baseColor * 0.2f);
                    }
                    // アルファ値は「溶け具合」に使ったので、描画自体は不透明(1.0)として出力
                    output.color.a = 1.0f;
                }
            // ===========================================================
            // ★追加 5. マグマ・覚醒 (Emissive)
            // ===========================================================
                else if (gMaterial.materialType == 5)
                {
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    
                    // 1. 画像の「暗い部分」を判定する (輝度計算)
                    float luminance = dot(baseColor, float3(0.299f, 0.587f, 0.114f));
                    
                    // 2. 「暗い部分」ほど強く光らせる (smoothstepで光る範囲を調整)
                    // (元のテクスチャの溝や影になっている部分からエネルギーが漏れ出す表現)
                    float glowFactor = smoothstep(0.4f, 0.0f, luminance);
                    
                    // 3. 怒りのオーラ色（赤〜オレンジ）エディターの色(Color)を乗算して色を変えられる！
                    float3 glowColor = float3(2.5f, 0.8f, 0.0f) * gMaterial.color.rgb;
                    
                    // 4. 通常の照明計算
                    float NdotL = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
                    float3 litColor = baseColor * gDirectionalLight.color.rgb * NdotL;
                    
                    // 5. 元の絵の上に発光を足し合わせる
                    output.color.rgb = litColor + (glowColor * glowFactor);
                    output.color.a = gMaterial.color.a * textureColor.a;
                }
            // ===========================================================
            // . トゥーン調 (Cel Shaded)
            // ===========================================================
                else if (gMaterial.materialType == 6)
                {
                    float3 N = normalize(input.normal);
                    float3 L = normalize(-gDirectionalLight.direction);
                    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
                    
                    float NdotL = dot(N, L);
                    
                    // 1. 光の階調化（リアルなグラデーションを捨てて、パキッとした影を作る）
                    // NdotL が 0.0 以上なら明るい(1.0)、0.0 未満なら暗い(0.3)、という2階調アニメ塗り
                    float celFactor = (NdotL > 0.0f) ? 1.0f : 0.3f;
                    
                    // 2. 輪郭線（アウトライン）の抽出
                    float NdotV = saturate(dot(N, V));
                    // 視線と法線が直角に近い(エッジ部分)なら 0.0 (黒)、それ以外は 1.0
                    float outline = (NdotV < 0.25f) ? 0.0f : 1.0f;
                    
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    float3 finalColor = baseColor * gDirectionalLight.color.rgb * celFactor;
                    
                    // 3. アニメ塗りの色に、黒い輪郭線を乗算する
                    output.color.rgb = finalColor * outline;
                    output.color.a = gMaterial.color.a * textureColor.a;
                }

            // ===========================================================
            // 通常のPBRマテリアル
            // ===========================================================
                else
                {
                
                 // ★大進化: ORMマップ(t4)から画像をサンプリング！
                    float3 ormColor = gOrmMap.Sample(gSampler, input.texcoord).rgb;

                    // 画像がない(white.png)時は 1.0 が掛かるのでスライダーの値がそのまま使われる！
                    float roughness = gMaterial.roughness * ormColor.g;
                    float metallic = gMaterial.metallic * ormColor.b;
                    
                    float3 N = normalize(input.normal);

                    if (gMaterial.enableNormalMap == 1)
                    {
                        float3 T = normalize(input.tangent);
                    
                    // グラム・シュミットの直交化 (NとTを確実に90度にする)
                        T = normalize(T - dot(T, N) * N);
                    // 従法線 (Binormal/Bitangent) の計算
                        float3 B = cross(N, T);
                    
                    // TBN行列の作成 (Tangent Space -> World Space)
                        float3x3 TBN = float3x3(T, B, N);
                    
                    // ノーマルマップの画像からRGBを取得 (0.0 ～ 1.0)
                        float3 normalMap = gNormalMap.Sample(gSampler, input.texcoord).rgb;
                    
                    // RGBを -1.0 ～ 1.0 のベクトルに変換
                        normalMap = normalMap * 2.0f - 1.0f;
                    
                    // ワールド空間の新しい法線（ねじ曲げられた法線）を計算！
                        N = normalize(mul(normalMap, TBN));
                    }
                    float3 albedo = gMaterial.color.rgb * textureColor.rgb;

                    float3 F0 = float3(0.04f, 0.04f, 0.04f);
                    F0 = lerp(F0, albedo, metallic);

                    float3 Lo = float3(0.0f, 0.0f, 0.0f);

                // 1. 平行光源
                    float3 L_dir = normalize(-gDirectionalLight.direction);
                    float3 radiance_dir = gDirectionalLight.color.rgb * gDirectionalLight.intenssity * shadowFactor;
                    Lo += CalcPBRLight(L_dir, V, N, radiance_dir, albedo, roughness, metallic, F0);

                // 2. 点光源
                    for (int i = 0; i < gPointLights.activeCount; ++i)
                    {
                        PointLight pLight = gPointLights.lights[i];
                        float3 L_point = pLight.position - input.worldPosition;
                        float distance = length(L_point);
                        L_point = normalize(L_point);
                    
                        float attenuation = pow(saturate(-distance / pLight.radius + 1.0f), pLight.decay);
                        float3 radiance_point = pLight.color.rgb * pLight.intensity * attenuation;
                        Lo += CalcPBRLight(L_point, V, N, radiance_point, albedo, roughness, metallic, F0);
                    }

                // 3. スポットライト
                    for (int j = 0; j < gSpotLights.activeCount; ++j)
                    {
                        SpotLight sLight = gSpotLights.lights[j];
                        float3 L_spot = sLight.position - input.worldPosition;
                        float distance = length(L_spot);
                        L_spot = normalize(L_spot);
                    
                        float distanceFactor = pow(saturate(-distance / sLight.distance + 1.0f), sLight.decay);
                        float angleCos = dot(-L_spot, normalize(sLight.direction));
                        float falloffFactor = saturate((angleCos - sLight.cosAngle) / (sLight.cosFalloffStart - sLight.cosAngle));
                        float3 radiance_spot = sLight.color.rgb * sLight.intensity * distanceFactor * falloffFactor;
                        Lo += CalcPBRLight(L_spot, V, N, radiance_spot, albedo, roughness, metallic, F0);
                    }

                // 4. 環境マップ (IBL)
                    float3 ambient = float3(0.0f, 0.0f, 0.0f);
                
                    if (gDirectionalLight.enableEnvMap == 1)
                    {
                        float3 R = reflect(-V, N);
                        const float MAX_REFLECTION_LOD = 5.0f;
                        float3 envColor = gEnvTexture.SampleLevel(gSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
                        envColor *= gDirectionalLight.envIntensity;

                        float3 F_ibl = FresnelSchlick(max(dot(N, V), 0.0f), F0);
                        float3 kS_ibl = F_ibl;
                        float3 kD_ibl = 1.0f - kS_ibl;
                        kD_ibl *= 1.0f - metallic;

                        float3 specular_ibl = envColor * F_ibl;
                        float3 diffuse_ibl = kD_ibl * albedo * gDirectionalLight.ambientColor;

                        ambient = diffuse_ibl + specular_ibl;
                    }
                    else
                    {
                        ambient = gDirectionalLight.ambientColor * albedo * (1.0f - metallic);
                    }

                    output.color.rgb = Lo + ambient;
                    output.color.a = gMaterial.color.a * textureColor.a;
                }

            // ===========================================================
            //  距離フォグ
            // ===========================================================
                float3 fogColor = gDirectionalLight.fogColor;
                float fogStart = gDirectionalLight.fogStart;
                float fogEnd = gDirectionalLight.fogEnd;

                float distanceToCamera = length(input.worldPosition - gCamera.worldPosition);
                float fogRange = max(fogEnd - fogStart, 0.01f);
                float fogFactor = saturate((distanceToCamera - fogStart) / fogRange);

                output.color.rgb = lerp(output.color.rgb, fogColor, fogFactor);

                break; 
            } 
    }

    return output;
}