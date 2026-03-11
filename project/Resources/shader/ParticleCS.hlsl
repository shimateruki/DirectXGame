// ==========================================================
// ParticleCS.hlsl : 神連携対応版
// ==========================================================
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
    float3 emitArea;
    float padding1;
    float3 emitVelocity;
    float velocityVariance;
    
    float4 baseColor;
    
    // --- ★ エディタからリアルタイムに送られてくる環境パラメータ ---
    float3 gravity;
    float drag;
    
    float3 wind;
    float turbulence;
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

    // 1. 今回のフレームで自分が「発生(エミット)」の対象かチェック
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

        // ★ 乱数を使って、emitPos を中心とした emitArea の範囲内に散らす
        float rX = rand(float2(index, time)) * 2.0f - 1.0f;
        float rY = rand(float2(time, index)) * 2.0f - 1.0f;
        float rZ = rand(float2(index * time, 1.0f)) * 2.0f - 1.0f;
        
        p.position = emitPos + float3(rX, rY, rZ) * emitArea;

        // 速度や色の処理はそのまま
        float r1 = rand(float2(index + 1.0f, time)) * 2.0f - 1.0f;
        float r2 = rand(float2(time + 1.0f, index)) * 2.0f - 1.0f;
        float r3 = rand(float2(index * time + 1.0f, 1.0f)) * 2.0f - 1.0f;
        
        p.velocity = emitVelocity + float3(r1, r2, r3) * velocityVariance;
        p.color = baseColor;
        p.color.xyz += float3(r1, r2, r3) * 0.1f;
        
        // ★ 発生直後は透明にしておく（フェードインの準備）
        p.color.a = 0.0f;
    }
    else if (p.life > 0.0f)
    {
        p.life -= deltaTime;
        
        // ... (重力や風の処理はそのまま) ...
        
        p.position += p.velocity * deltaTime;
        
        // ★ フェードイン＆フェードアウトの完璧な計算
        // 寿命の残り割合 (1.0 = 生まれたて, 0.0 = 死ぬ直前)
        float lifeRatio = saturate(p.life / p.maxLife);
        
        // smoothstepを使って、最初と最後にフワッと消える山なりのカーブを作る
        // 例: 0.8～1.0の間でフェードイン、0.0～0.2の間でフェードアウト
        float alphaFade = smoothstep(0.0f, 0.2f, lifeRatio) * (1.0f - smoothstep(0.8f, 1.0f, lifeRatio));
        
        p.color.a = alphaFade;
    }

    // 計算結果をメモリに書き戻す
    particles[index] = p;
}