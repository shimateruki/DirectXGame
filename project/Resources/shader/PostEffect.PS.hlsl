struct PSInput
{
	float4 svpos : SV_POSITION;
	float2 uv : TEXCOORD;
};

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

cbuffer PostEffectParams : register(b0)
{
	float threshold; // 輝度の閾値 (例:0.8)
	float bloomIntensity; // 光の強さ (例:2.0)
	float spread; // ぼかしの広がり (例:2.0)
	float padding; // 16バイトに合わせるための詰め物
};

float4 main(PSInput input) : SV_TARGET
{
	float4 baseColor = tex.Sample(smp, input.uv);
	float4 bloomColor = float4(0.0, 0.0, 0.0, 0.0);
	float sampleCount = 0.0;
    
	float dx = 1.0 / 1280.0;
	float dy = 1.0 / 720.0;

	for (int x = -2; x <= 2; x++)
	{
		for (int y = -2; y <= 2; y++)
		{
            // C++から送られた spread を使う
			float2 offset = float2(x * dx, y * dy) * spread;
			float4 sampleColor = tex.Sample(smp, input.uv + offset);
            
			float brightness = dot(sampleColor.rgb, float3(0.299, 0.587, 0.114));
            
            // ★進化：if文をやめる！
            // brightness が threshold(0.8) より大きければ、その差分だけ抽出。
            // 0.8以下なら結果は 0.0 になる。これで滑らかに消えます！
			float extract = max(0.0, brightness - threshold);
            
			bloomColor += sampleColor * extract;
			sampleCount += 1.0;
		}
	}
    
	bloomColor /= sampleCount;

    // C++から送られた bloomIntensity を使う
	return baseColor + (bloomColor * bloomIntensity);
}