struct Particle
{
    float3 position;
    float life;
    float3 velocity;
    float maxLife;
    float4 color;
    float scale;
    float rotation;
    float rotSpeed;
    float padding;
};

RWStructuredBuffer<Particle> particles : register(u0);

cbuffer Config : register(b0)
{
    float deltaTime;
    float time;
    uint startIndex;
    uint emitCount;
    
    float3 emitPos;
    float emitLife;
    
    float3 emitArea;
    float padding1;
    
    float3 emitVelocity;
    float velocityVariance;
    
    float4 baseColor;
    
    float3 gravity;
    float drag;
    
    float3 wind;
    float turbulence;
    
    // --- ★ここを修正！ ---
    float baseSize; // 発生時の大きさ
    float endSize; // 消滅時の大きさ（拡散）
    float rotSpeedVariance; // ★追加: これがないとエラーになります！
    float padding2; // ★修正: float2 から float に変更（アライメント用）
    float4 endColor;
};
// HLSLで超高速に乱数を生成する魔法の関数
float rand(float2 seed)
{
    return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    Particle p = particles[index];

    // 1. エミット対象かチェック
    bool shouldEmit = false;
    if (emitCount > 0)
    {
        uint endIndex = startIndex + emitCount;
        if (endIndex <= 100000)
        {
            if (index >= startIndex && index < endIndex)
                shouldEmit = true;
        }
        else
        {
            if (index >= startIndex || index < (endIndex % 100000))
                shouldEmit = true;
        }
    }

    // 2. 状態に応じた処理
    if (shouldEmit)
    {
        p.life = emitLife;
        p.maxLife = emitLife;

        // 乱数の規則性を散らして範囲内に配置
        float rX = rand(float2(index * 1.34f, time)) * 2.0f - 1.0f;
        float rY = rand(float2(time * 1.57f, index)) * 2.0f - 1.0f;
        float rZ = rand(float2(index * 1.89f, time * 2.13f)) * 2.0f - 1.0f;
        p.position = emitPos + float3(rX, rY, rZ) * emitArea;

        float r1 = rand(float2(index + 1.0f, time * 1.1f)) * 2.0f - 1.0f;
        float r2 = rand(float2(time + 1.2f, index * 1.3f)) * 2.0f - 1.0f;
        float r3 = rand(float2(index * time + 1.4f, 1.5f)) * 2.0f - 1.0f;
        p.velocity = emitVelocity + float3(r1, r2, r3) * velocityVariance;
        
        p.color = baseColor;
        p.color.a = 0.0f; // フェードイン準備
        
        p.rotation = rand(float2(index, time)) * 6.28318f;
        p.rotSpeed = (rand(float2(time, index)) * 2.0f - 1.0f) * rotSpeedVariance;
        p.scale = baseSize;
    }
    else if (p.life > 0.0f)
    {
        p.life -= deltaTime;
        
        p.velocity += gravity * deltaTime;
        p.velocity += wind * deltaTime;
        
        float3 noiseVec = float3(
            rand(p.position.xy + time) * 2.0f - 1.0f,
            rand(p.position.yz - time) * 2.0f - 1.0f,
            rand(p.position.zx + time) * 2.0f - 1.0f
        );
        p.rotation += p.rotSpeed * deltaTime;
        p.velocity += noiseVec * turbulence * deltaTime;
        p.velocity *= drag;
        p.position += p.velocity * deltaTime;
        
        float lifeRatio = saturate(p.life / p.maxLife);
        
        //  時間経過で大きさを変える (0に近づくほど endSize になる)
        p.scale = lerp(endSize, baseSize, lifeRatio);
        p.color.rgb = lerp(endColor.rgb, baseColor.rgb, lifeRatio);
        // 滑らかなフェードイン・フェードアウト
        float alphaFade = smoothstep(0.0f, 0.2f, lifeRatio) * (1.0f - smoothstep(0.8f, 1.0f, lifeRatio));
        p.color.a = alphaFade;
    }

    particles[index] = p;
}