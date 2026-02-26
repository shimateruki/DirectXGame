// C++側と同じ構造体
struct Particle
{
    float3 position;
    float life;
    float3 velocity;
    float maxLife;
    float4 color;
};

RWStructuredBuffer<Particle> particles : register(u0);

// ★ C++側の CSConfig に合わせた定数バッファ
cbuffer Config : register(b0)
{
    float deltaTime;
    float time;
    uint startIndex;
    uint emitCount;
    float3 emitPos;
    float emitLife;
    float3 emitVelocity;
    float velocityVariance;
    float4 baseColor;
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

    // 1. 今回のフレームで自分が「発生(エミット)」の対象かチェック (リングバッファ判定)
    bool shouldEmit = false;
    if (emitCount > 0)
    {
        uint endIndex = startIndex + emitCount;
        if (endIndex <= 100000)
        { // kMaxParticles
            if (index >= startIndex && index < endIndex)
                shouldEmit = true;
        }
        else
        {
            // インデックスが10万を超えて0に戻った（ラップアラウンドした）場合
            if (index >= startIndex || index < (endIndex % 100000))
                shouldEmit = true;
        }
    }

    // 2. 状態に応じた処理
    if (shouldEmit)
    {
        // --- ★ 復活処理 (初期化) ---
        p.life = emitLife;
        p.maxLife = emitLife;
        p.position = emitPos;

        // ランダムな方向に散らす (シード値に自分のインデックスを使って全員違う乱数にする)
        float r1 = rand(float2(index, time)) * 2.0f - 1.0f;
        float r2 = rand(float2(time, index)) * 2.0f - 1.0f;
        float r3 = rand(float2(index * time, 1.0f)) * 2.0f - 1.0f;
        
        p.velocity = emitVelocity + float3(r1, r2, r3) * velocityVariance;
        p.color = baseColor;
        p.color.xyz += float3(r1, r2, r3) * 0.1f;
    }
    else if (p.life > 0.0f)
    {
        // --- ★ 既存の粒の更新処理 ---
        p.life -= deltaTime;
        
        // 重力 (下に落ちる)
        p.velocity.y -= 9.8f * deltaTime * 0.2f;
        
        // 空気抵抗 (だんだん遅くなる)
        p.velocity *= 0.98f;
        
        p.position += p.velocity * deltaTime;
        
        // 寿命に合わせて透明にする
        p.color.a = saturate(p.life / p.maxLife);
    }

    // 計算結果をメモリに書き戻す
    particles[index] = p;
}