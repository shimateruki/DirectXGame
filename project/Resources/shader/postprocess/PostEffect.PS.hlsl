struct PSInput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float threshold;
    float bloomIntensity;
    float spread;
    int enableToneMapping;
    
    float vignetteIntensity;
    float chromaticAberration;
    float filmGrainIntensity;
    float vignettePower;

    float time;
    
    float radialCenterX;
    float radialCenterY;
    float radialIntensity;
    int radialBlurSamples;
    
    float lutIntensity;
    float colorExposure;
    float colorContrast;
    float colorSaturation;
    float colorTemperature;
    float colorTint;
    float damageFlash;
    float cinemaBarHeight;
    float wobbleIntensity;
    
    float scanlineIntensity;
    float mosaicSize;
    float dangerVignette;
    float blackout;
    float grayscaleIntensity;
    float sepiaIntensity;
    int boxFilterSize;
    int gaussianFilterSize;
    float gaussianSigma;
    float luminanceOutlineIntensity;
    float depthOutlineIntensity;
    float dissolveThreshold;
    float dissolveEdgeWidth;
    float randomIntensity;
    float padding_m1;
    float3 dissolveEdgeColor;
    float padding_m2;
    float4x4 projectionInverse;

    // --- Slime Fade ---
    float slimeFadeIntensity;
    float slimeDensity;
    float padding_s1;
    float padding_s2;
    float3 slimeColor;
    float padding_s3;

    // --- Iris Out ---
    float irisFadeIntensity;
    float irisCenterX;
    float irisCenterY;
    float padding_i1;
};

Texture2D<float4> lutTex : register(t1);
Texture2D<float4> depthTex : register(t2);
Texture2D<float> noiseTex : register(t3);
SamplerState pointSmp : register(s1);

// --- 1. Copy ---
float4 mainCopy(PSInput input) : SV_TARGET
{
    return tex.Sample(smp, input.uv);
}

float4 mainExtract(PSInput input) : SV_TARGET
{
    float4 color = tex.Sample(smp, input.uv);
    float brightness = dot(color.rgb, float3(0.299, 0.587, 0.114));
    
    // 閾値を超えた分の「割合」を計算する（HDR対応の安全な計算）
    float contribution = max(0.0, brightness - threshold);
    contribution /= max(brightness, 0.00001); // 0除算防止
    
    return float4(color.rgb * contribution, 1.0);
}

// --- 3. Downsample ---
float4 mainDownsample(PSInput input) : SV_TARGET
{
    uint w, h;
    tex.GetDimensions(w, h);
    float dx = 1.0 / float(w) * spread;
    float dy = 1.0 / float(h) * spread;
    
    float4 color = float4(0, 0, 0, 0);
    color += tex.Sample(smp, input.uv) * 0.5;
    color += tex.Sample(smp, input.uv + float2(-dx, -dy)) * 0.125;
    color += tex.Sample(smp, input.uv + float2(dx, -dy)) * 0.125;
    color += tex.Sample(smp, input.uv + float2(-dx, dy)) * 0.125;
    color += tex.Sample(smp, input.uv + float2(dx, dy)) * 0.125;
    
    return float4(color.rgb, 1.0);
}

// --- 4. Add ---
float4 mainAdd(PSInput input) : SV_TARGET
{
    float4 color = tex.Sample(smp, input.uv);
    return float4(color.rgb * bloomIntensity, 1.0);
}

// --- Utility Functions ---
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float rand(float2 co)
{
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

float3 ApplyLUT(float3 color)
{
    float blue = saturate(color.b) * 15.0;
    float block1 = floor(blue);
    float block2 = ceil(blue);
    float fracBlue = frac(blue);
    
    float u1 = (block1 * 16.0 + saturate(color.r) * 15.0 + 0.5) / 256.0;
    float v1 = (saturate(color.g) * 15.0 + 0.5) / 16.0;
    float u2 = (block2 * 16.0 + saturate(color.r) * 15.0 + 0.5) / 256.0;
    float v2 = (saturate(color.g) * 15.0 + 0.5) / 16.0;
    
    float3 color1 = lutTex.Sample(smp, float2(u1, v1)).rgb;
    float3 color2 = lutTex.Sample(smp, float2(u2, v2)).rgb;
    
    return lerp(color1, color2, fracBlue);
}

float Luminance(float3 v)
{
    return dot(v, float3(0.2125f, 0.7154f, 0.0721f));
}

float3 ApplyColorGrading(float3 color)
{
    color = max(color * exp2(colorExposure), 0.0f);
    color = (color - 0.5f) * max(colorContrast, 0.0f) + 0.5f;

    float luminance = Luminance(color);
    color = lerp(float3(luminance, luminance, luminance), color, max(colorSaturation, 0.0f));

    float temperatureAmount = saturate(abs(colorTemperature));
    float3 warmBalance = float3(1.10f, 1.03f, 0.92f);
    float3 coolBalance = float3(0.92f, 1.02f, 1.10f);
    float3 temperatureBalance = (colorTemperature >= 0.0f) ? warmBalance : coolBalance;
    color *= lerp(float3(1.0f, 1.0f, 1.0f), temperatureBalance, temperatureAmount);

    color += float3(colorTint * 0.025f, colorTint * 0.050f, -colorTint * 0.025f);
    return saturate(color);
}

// ガウス関数 (資料に基づいた実装)
float gauss(float x, float y, float sigma)
{
    float sigma2 = sigma * sigma;
    return (1.0f / (2.0f * 3.14159265f * sigma2)) * exp(-(x * x + y * y) / (2.0f * sigma2));
}

// ぼかし（Box/Gaussian Filter）を含めたサンプリング関数
float3 SampleScene(float2 uv)
{
    uint w, h;
    tex.GetDimensions(w, h);
    float2 stepSize = 1.0f / float2(w, h);

    // 1. Gaussian Filter (資料に基づいた実装)
    if (gaussianFilterSize > 0)
    {
        float3 blurColor = float3(0, 0, 0);
        float totalWeight = 0.0f;
        int n = gaussianFilterSize;
        
        for (int x = -n; x <= n; ++x)
        {
            for (int y = -n; y <= n; ++y)
            {
                float weight = gauss((float)x, (float)y, gaussianSigma);
                blurColor += tex.Sample(smp, uv + float2(x, y) * stepSize).rgb * weight;
                totalWeight += weight;
            }
        }
        return blurColor / totalWeight;
    }

    // 2. Box Filter (資料に基づいた実装)
    if (boxFilterSize > 0)
    {
        float3 blurColor = float3(0, 0, 0);
        int n = boxFilterSize; // 1:3x3, 2:5x5...
        float kernelCount = (n * 2 + 1) * (n * 2 + 1);
        
        for (int x = -n; x <= n; ++x)
        {
            for (int y = -n; y <= n; ++y)
            {
                blurColor += tex.Sample(smp, uv + float2(x, y) * stepSize).rgb;
            }
        }
        return blurColor / kernelCount;
    }
    
    return tex.Sample(smp, uv).rgb;
}

// --- 5. Final Composite ---
float4 mainComposite(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;

    // Wobble (波打ち)
    if (wobbleIntensity > 0.0)
    {
        uv.x += sin(uv.y * 40.0 + time * 15.0) * wobbleIntensity;
    }

    // Mosaic (ドット絵化)
    if (mosaicSize > 1.0)
    {
        uint w, h;
        tex.GetDimensions(w, h);
        float2 res = float2(w, h) / mosaicSize;
        uv = (floor(uv * res) + 0.5) / res;
    }

    // Radial Blur & Chromatic Aberration
    float2 center = float2(0.5, 0.5);
    float2 dir = uv - center;
    float2 radialCenter = float2(radialCenterX, radialCenterY);
    float2 radialDir = uv - radialCenter;

    float4 baseColor = float4(0, 0, 0, 0);
    int NUM_SAMPLES = 8;

    if (radialIntensity > 0.0)
    {
        // 放射ブラー (資料に基づいた実装)
        float step = radialIntensity / (float) radialBlurSamples;
        for (int i = 0; i < radialBlurSamples; i++)
        {
            float2 offsetUv = uv - radialDir * (i * step);
            // 色収差も含めてサンプリング
            float r = tex.Sample(smp, offsetUv - dir * chromaticAberration).r;
            float g = tex.Sample(smp, offsetUv).g;
            float b = tex.Sample(smp, offsetUv + dir * chromaticAberration).b;
            baseColor += float4(r, g, b, 1.0);
        }
        baseColor /= (float) radialBlurSamples;
    }
    else
    {
        float r = SampleScene(uv - dir * chromaticAberration).r;
        float g = SampleScene(uv).g;
        float b = SampleScene(uv + dir * chromaticAberration).b;
        baseColor = float4(r, g, b, 1.0);
    }

    float4 finalColor = baseColor;

    // Tone Mapping
    if (enableToneMapping == 1)
    {
        finalColor.rgb = ACESFilm(finalColor.rgb);
    }
    else if (enableToneMapping == 2)
    {
        float luminance = dot(finalColor.rgb, float3(0.299, 0.587, 0.114));
        float mappedLuminance = ACESFilm(float3(luminance, luminance, luminance)).r;
        finalColor.rgb = finalColor.rgb * (mappedLuminance / (luminance + 0.0001));
    }
    else
    {
        finalColor = saturate(finalColor);
    }

    finalColor.rgb = ApplyColorGrading(finalColor.rgb);

    // LUT
    if (lutIntensity > 0.0)
    {
        float3 lutColor = ApplyLUT(finalColor.rgb);
        finalColor.rgb = lerp(finalColor.rgb, lutColor, lutIntensity);
    }
    if (dangerVignette > 0.0)
    {
        // 1. 画面端に行くほど値が大きくなるマスクを作る
        // dot(dir, dir)は中心で0.0、四隅で0.5になるので2倍して調整
        float edgeMask = saturate(dot(dir, dir) * 2.0);
        
        // 2. timeを使ってドクン…ドクン…という鼓動(サイン波)を作る
        // sin関数を使って0.4 ~ 1.0の間で脈打たせる
        float pulse = 0.4 + 0.6 * saturate(sin(time * 5.0));
        
        // 3. 少しドス黒い血のような赤色を定義
        float3 bloodColor = float3(0.8, 0.0, 0.0);
        
        // 4. 元の映像に、血の色をブレンドする
        finalColor.rgb = lerp(finalColor.rgb, bloodColor, edgeMask * dangerVignette * pulse);
    }

    // Outline (資料に基づいた実装)
    if (luminanceOutlineIntensity > 0.0 || depthOutlineIntensity > 0.0)
    {
        uint w, h;
        tex.GetDimensions(w, h);
        float2 stepSize = 1.0f / float2(w, h);
        
        float2 diffL = 0; // Luminance difference
        float2 diffD = 0; // Depth difference
        
        static const float kPrewittH[3][3] =
        {
            {-1.0f / 6.0f, 0.0f, 1.0f / 6.0f},
            {-1.0f / 6.0f, 0.0f, 1.0f / 6.0f},
            {-1.0f / 6.0f, 0.0f, 1.0f / 6.0f}
        };
        static const float kPrewittV[3][3] =
        {
            {-1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f},
            {0.0f, 0.0f, 0.0f},
            {1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f}
        };
        
        for (int x = 0; x < 3; ++x)
        {
            for (int y = 0; y < 3; ++y)
            {
                float2 offsetUv = uv + float2(x - 1, y - 1) * stepSize;
                
                // 1. 輝度ベース
                if (luminanceOutlineIntensity > 0.0)
                {
                    float3 fetch = tex.Sample(smp, offsetUv).rgb;
                    float l = Luminance(fetch);
                    diffL.x += l * kPrewittH[x][y];
                    diffL.y += l * kPrewittV[x][y];
                }
                
                // 2. 深度ベース
                if (depthOutlineIntensity > 0.0)
                {
                    float ndcDepth = depthTex.Sample(pointSmp, offsetUv).r;
                    float4 viewSpace = mul(float4(0, 0, ndcDepth, 1.0f), projectionInverse);
                    float viewZ = viewSpace.z / viewSpace.w;
                    diffD.x += viewZ * kPrewittH[x][y];
                    diffD.y += viewZ * kPrewittV[x][y];
                }
            }
        }
        
        float edgeL = length(diffL);
        float edgeD = length(diffD);
        
        // 資料に基づいた重み調整
        float weightL = saturate(edgeL * 6.0f) * luminanceOutlineIntensity;
        float weightD = saturate(edgeD) * depthOutlineIntensity;
        
        float weight = max(weightL, weightD);
        // 黒い縁取りとして合成
        finalColor.rgb = lerp(finalColor.rgb, float3(0, 0, 0), weight);
    }

    // Dissolve (資料に基づいた実装)
    if (dissolveThreshold > 0.0)
    {
        float mask = noiseTex.Sample(smp, uv).r;
        
        // 1. しきい値以下を抜く
        if (mask <= dissolveThreshold)
        {
            discard;
        }
        
        // 2. エッジ（境界線）の色付け
        float edge = 1.0f - smoothstep(dissolveThreshold, dissolveThreshold + dissolveEdgeWidth, mask);
        finalColor.rgb += edge * dissolveEdgeColor;
    }

    // Random (資料に基づいた実装)
    if (randomIntensity > 0.0)
    {
        // 昔のテレビのような砂嵐（高周波なノイズ）を再現
        // UVに大きな値を掛け、時間を加算することで、激しいチラつきを作る
        float2 seed = uv * 1000.0f + float2(time * 10.0f, -time * 7.0f);
        float noise = rand(seed);
        finalColor.rgb = lerp(finalColor.rgb, float3(noise, noise, noise), randomIntensity);
    }

    // Vignette & Film Grain
    // Vignette 
    float2 correct = uv * (1.0f - uv.yx);
    float v = correct.x * correct.y * 16.0f;
    v = saturate(pow(v, vignettePower));
    // vignetteIntensity で適用度を調整
    finalColor.rgb *= lerp(1.0f, v, vignetteIntensity);

    finalColor.rgb -= rand(uv + time) * filmGrainIntensity;
    if (blackout > 0.0)
    {
        // 画面全体を黒に近づける（1.0 で完全な漆黒になる）
        finalColor.rgb *= (1.0 - saturate(blackout));
    }

    // Slime Fade (Drip)
    if (slimeFadeIntensity > 0.0)
    {
        // --- 1. 有機的な滴り形状の計算 ---
        // 複数のサイン波を重ねて「ポテッ」とした厚みのある滴りを作る
        float dripShape = sin(uv.x * 5.0) * 0.05 + sin(uv.x * 12.0) * 0.02;
        dripShape += noiseTex.Sample(smp, float2(uv.x * 0.5, time * 0.05)).r * 0.1;
        
        // 進捗 (0.0 -> 1.0) に合わせて上から降りてくる。少し余裕を持たせて完全に覆う。
        float progress = slimeFadeIntensity * 1.3; 
        float thresholdY = progress - 0.15 + dripShape;
        
        // 境界線のマイルドなぼかし
        float mask = smoothstep(thresholdY - 0.02, thresholdY + 0.02, uv.y);
        mask = 1.0 - mask; // 上から下へ
        
        if (mask > 0.0)
        {
            // --- 2. 屈折と歪み ---
            // エッジ付近を強く歪ませて液体のレンズ効果を出す
            float distortionIntensity = mask * (1.0 - mask) * 4.0;
            float2 distort = float2(
                sin(uv.y * 20.0 + time * 2.0),
                cos(uv.x * 20.0 + time * 2.0)
            ) * 0.01 * distortionIntensity;
            
            float3 background = tex.Sample(smp, uv + distort).rgb;
            
            // --- 3. スライムの色と透明度 ---
            // 中心部は濃く、エッジは少し透けるように
            float3 baseSlime = slimeColor;
            float interior = smoothstep(0.0, 0.3, mask);
            float3 finalSlime = lerp(background, baseSlime, interior * 0.9);
            
            // --- 4. 気泡 (Bubbles) ---
            // 擬似的な気泡を描画
            float2 bubbleUv = uv * float2(10.0, 5.0) + float2(0, time * 0.2);
            float bNoise = rand(floor(bubbleUv));
            if (bNoise > 0.95) { // 5%の確率で泡
                float2 bPos = frac(bubbleUv) - 0.5;
                float bDist = length(bPos);
                float bubble = smoothstep(0.1, 0.08, bDist); // 小さな丸
                finalSlime += bubble * 0.3 * (1.0 - interior); // 泡を白っぽく
            }
            
            // --- 5. 光沢・ハイライト (Gloss) ---
            // 滴りの先端（エッジ）にヌルヌルした光沢を入れる
            float gloss = pow(distortionIntensity, 3.0) * 0.5;
            finalSlime += gloss * float3(1, 1, 1);
            
            finalColor.rgb = finalSlime;
        }
    }

    // Grayscale / Sepia (資料に基づいた実装)
    if (grayscaleIntensity > 0.0 || sepiaIntensity > 0.0)
    {
        // 人間の目の感度に基づいた輝度計算 (BT709)
        float gray = dot(finalColor.rgb, float3(0.2125f, 0.7154f, 0.0721f));
        
        // 1. Grayscaleを適用
        float3 grayscale = float3(gray, gray, gray);
        finalColor.rgb = lerp(finalColor.rgb, grayscale, grayscaleIntensity);
        
        // 2. Sepiaを適用 (Grayscale化した色にセピアの色調を乗算)
        // RGB(107, 74, 43) を赤成分が1.0になるように正規化した比率
        float3 sepiaScale = float3(1.0f, 74.0f / 107.0f, 43.0f / 107.0f);
        float3 sepia = grayscale * sepiaScale;
        finalColor.rgb = lerp(finalColor.rgb, sepia, sepiaIntensity);
    }
    // Scanline (ブラウン管)
    if (scanlineIntensity > 0.0)
    {
        float scanline = sin(input.uv.y * 1000.0) * 0.5 + 0.5;
        finalColor.rgb -= scanline * 0.15 * scanlineIntensity;
    }

    // Damage Flash
    if (damageFlash > 0.0)
    {
        finalColor.rgb = lerp(finalColor.rgb, float3(1.0, 0.0, 0.0), damageFlash);
    }

    // Iris Out (Mario Galaxy style)
    if (irisFadeIntensity > 0.0)
    {
        float2 irisCenter = float2(irisCenterX, irisCenterY);
        float2 toCenter = input.uv - irisCenter;
        
        // アスペクト比補正 (画面が横長なので、そのまま計算すると楕円になる)
        uint w, h;
        tex.GetDimensions(w, h);
        toCenter.x *= (float)w / (float)h;
        
        float dist = length(toCenter);
        
        // irisFadeIntensity (0.0 -> 1.0) に応じて半径を小さくする
        // 1.0 (対角線より少し大きい) -> 0.0
        float radius = (1.0 - irisFadeIntensity) * 0.8;
        
        // 円の外側を黒く塗る
        float circleMask = smoothstep(radius, radius - 0.01, dist);
        finalColor.rgb *= circleMask;
    }

    // Cinema Bars (元のUVを使って歪みを防ぐ)
    if (input.uv.y < cinemaBarHeight || input.uv.y > 1.0 - cinemaBarHeight)
    {
        finalColor.rgb = float3(0.0, 0.0, 0.0);
    }

    return finalColor;
}
