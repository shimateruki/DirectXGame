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
    float time;
    
    float radialCenterX;
    float radialCenterY;
    float radialIntensity;
    float radialPadding;
    
    float lutIntensity;
    float damageFlash;
    float cinemaBarHeight;
    float wobbleIntensity;
    
    float scanlineIntensity;
    float mosaicSize;
    float dangerVignette;
    float blackout;
    float crtShutdown; // ★ 変更: padding1 を crtShutdown にリネーム
};

Texture2D<float4> lutTex : register(t1);

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

// --- 5. Final Composite ---
float4 mainComposite(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;
    
    // ★ エラー回避：定数バッファの変数をローカル変数に移して操作可能にする
    float currentAberration = chromaticAberration;
    float currentScanline = scanlineIntensity;
    
    // ===================================================
    // ★ 電脳リブート（CRT Shutdown）演出
    // ===================================================
    float isOutOfBounds = 0.0;
    float crtBrightness = 1.0;
    
    if (crtShutdown > 0.0)
    {
        // 前半(0.0~0.6): ノイズと色収差（RGBズレ）を極端に強める
        float glitchPhase = saturate(crtShutdown / 0.6);
        float noise = rand(float2(uv.y * 15.0, time)) - 0.5;
        uv.x += noise * 0.03 * glitchPhase;
        
        currentAberration += 0.05 * glitchPhase;
        currentScanline = max(currentScanline, 0.8 * glitchPhase);
        
        // 後半(0.6~1.0): 画面が縦に圧縮され、強烈に発光する
        float squashPhase = saturate((crtShutdown - 0.6) / 0.4);
        float currentHeight = max(1.0 - pow(squashPhase, 3.0), 0.001);
        
        uv.y = (uv.y - 0.5) / currentHeight + 0.5;
        
        if (uv.y < 0.0 || uv.y > 1.0)
        {
            isOutOfBounds = 1.0;
        }
        else
        {
            crtBrightness += squashPhase * 8.0;
        }
    }

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
        float step = radialIntensity / (float) NUM_SAMPLES;
        for (int i = 0; i < NUM_SAMPLES; i++)
        {
            float2 offsetUv = uv - radialDir * (i * step);
            // ★ local変数を使用
            float r = tex.Sample(smp, offsetUv - dir * currentAberration).r;
            float g = tex.Sample(smp, offsetUv).g;
            float b = tex.Sample(smp, offsetUv + dir * currentAberration).b;
            baseColor += float4(r, g, b, 1.0);
        }
        baseColor /= (float) NUM_SAMPLES;
    }
    else
    {
        // ★ local変数を使用
        float r = tex.Sample(smp, uv - dir * currentAberration).r;
        float g = tex.Sample(smp, uv).g;
        float b = tex.Sample(smp, uv + dir * currentAberration).b;
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

    // LUT
    if (lutIntensity > 0.0)
    {
        float3 lutColor = ApplyLUT(finalColor.rgb);
        finalColor.rgb = lerp(finalColor.rgb, lutColor, lutIntensity);
    }
    
    // Danger Vignette
    if (dangerVignette > 0.0)
    {
        float edgeMask = saturate(dot(dir, dir) * 2.0);
        float pulse = 0.4 + 0.6 * saturate(sin(time * 5.0));
        float3 bloodColor = float3(0.8, 0.0, 0.0);
        finalColor.rgb = lerp(finalColor.rgb, bloodColor, edgeMask * dangerVignette * pulse);
    }
    
    // Vignette & Film Grain
    float v = 1.0 - dot(dir, dir) * vignetteIntensity;
    finalColor.rgb *= saturate(v);
    finalColor.rgb -= rand(uv + time) * filmGrainIntensity;
    
    // Blackout
    if (blackout > 0.0)
    {
        finalColor.rgb *= (1.0 - saturate(blackout));
    }
    
    // Scanline (ブラウン管)
    if (currentScanline > 0.0) // ★ local変数を使用
    {
        float scanline = sin(input.uv.y * 1000.0) * 0.5 + 0.5;
        finalColor.rgb -= scanline * 0.15 * currentScanline;
    }

    // Damage Flash
    if (damageFlash > 0.0)
    {
        finalColor.rgb = lerp(finalColor.rgb, float3(1.0, 0.0, 0.0), damageFlash);
    }

    // Cinema Bars
    if (input.uv.y < cinemaBarHeight || input.uv.y > 1.0 - cinemaBarHeight)
    {
        finalColor.rgb = float3(0.0, 0.0, 0.0);
    }

    // ===================================================
    // ★ CRT Shutdown の画面外と発光の最終適用
    // ===================================================
    if (isOutOfBounds > 0.0)
    {
        finalColor.rgb = float3(0.0, 0.0, 0.0); // 潰れた外側は漆黒
    }
    else
    {
        finalColor.rgb *= crtBrightness; // 潰れた中央部分は強烈に発光
    }

    return finalColor;
}