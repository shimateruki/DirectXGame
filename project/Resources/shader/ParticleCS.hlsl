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
    

    float baseSize;
    float midSize;
    float endSize;
    float sizeMidTime;
    
    float4 midColor;
    
    float colorMidTime;
    float rotSpeedVariance;
    float2 padding2; // 8バイトの隙間埋め
    
    float4 endColor;

    // ===================================
    // 形状データ
    // ===================================
    uint shapeType;
    float shapeRadius;
    float shapeAngle;
    float padding3;
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

        // 共通で使う乱数
        float rX = rand(float2(index * 1.34f, time)) * 2.0f - 1.0f;
        float rY = rand(float2(time * 1.57f, index)) * 2.0f - 1.0f;
        float rZ = rand(float2(index * 1.89f, time * 2.13f)) * 2.0f - 1.0f;
        float r1 = rand(float2(index + 1.0f, time * 1.1f)) * 2.0f - 1.0f;
        float r2 = rand(float2(time + 1.2f, index * 1.3f)) * 2.0f - 1.0f;
        float r3 = rand(float2(index * time + 1.4f, 1.5f)) * 2.0f - 1.0f;

        // ========================================================
        // ★ 形状ごとの発生アルゴリズム
        // ========================================================
        if (shapeType == 1) // 🟢 Sphere (球体)
        {
            // 球の内部にランダム配置
            float3 dir = normalize(float3(rX, rY, rZ));
            float dist = rand(float2(index, time * 0.5f)) * shapeRadius;
            p.position = emitPos + dir * dist;

            // 外側に向かって弾け飛ぶ！(初期速度 + 放射状の速度)
            p.velocity = emitVelocity + (dir * velocityVariance);
        }
        else if (shapeType == 2) // 🔺 Cone (円錐)
        {
            float theta = rand(float2(time, index * 2.1f)) * 6.28318f;
            float rad = sqrt(rand(float2(index, time * 3.4f)));

            // 円錐の広がり角度からベクトルを計算
            float spread = tan(radians(shapeAngle));
            float2 localCircle = float2(cos(theta), sin(theta)) * rad;
            float3 localDir = normalize(float3(localCircle.x * spread, localCircle.y * spread, 1.0f));

            // 指定された emitVelocity の方向を「前(Z)」とする基底ベクトルを作成
            float3 forward = length(emitVelocity) > 0.001f ? normalize(emitVelocity) : float3(0, 1, 0);
            float3 right = abs(forward.y) < 0.999f ? normalize(cross(float3(0, 1, 0), forward)) : float3(1, 0, 0);
            float3 upAxis = cross(forward, right);

            // ローカルの円錐方向をワールドの向いている方向に回転させる
            float3 worldDir = localDir.x * right + localDir.y * upAxis + localDir.z * forward;

            // 発生位置は円錐の根本 (少し散らす場合はRadiusを使う)
            p.position = emitPos + (localCircle.x * right + localCircle.y * upAxis) * shapeRadius;

            // 速度は Velocity の長さを基準に、ワールド方向へ飛ばす
            float speed = length(emitVelocity) + r1 * velocityVariance;
            p.velocity = worldDir * speed;
        }
        else // 🟦 Box (四角形 - デフォルト)
        {
            p.position = emitPos + float3(rX, rY, rZ) * emitArea;
            p.velocity = emitVelocity + float3(r1, r2, r3) * velocityVariance;
        }

        // ========================================================
        // その他の初期化
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

        // ========================================================
        // ★ 魔法: 3点カーブ（時間経過の支配）
        // ========================================================
        // 0.0 (発生直後) ～ 1.0 (消滅寸前) の進行度を計算
        float ageRatio = 1.0f - saturate(p.life / p.maxLife);
        
        // 1. サイズの3点カーブ計算
        if (ageRatio < sizeMidTime)
        {
            // Base -> Mid へ向かうフェーズ
            float t = ageRatio / max(sizeMidTime, 0.001f);
            p.scale = lerp(baseSize, midSize, t);
        }
        else
        {
            // Mid -> End へ向かうフェーズ
            float t = (ageRatio - sizeMidTime) / max(1.0f - sizeMidTime, 0.001f);
            p.scale = lerp(midSize, endSize, t);
        }
        
        // 2. カラー＆透明度（Alpha）の3点カーブ計算
        if (ageRatio < colorMidTime)
        {
            float t = ageRatio / max(colorMidTime, 0.001f);
            p.color = lerp(baseColor, midColor, t);
        }
        else
        {
            float t = (ageRatio - colorMidTime) / max(1.0f - colorMidTime, 0.001f);
            p.color = lerp(midColor, endColor, t);
        }
    }

    particles[index] = p;
}