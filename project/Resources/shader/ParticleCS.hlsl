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
    uint configIndex;
    float4 memBaseColor;
    float4 memMidColor;
    float4 memEndColor;
    float memBaseSize;
    float memMidSize;
    float memEndSize;
    float memColorMidTime;
    float memSizeMidTime;
    uint memColorEaseType;
    uint memSizeEaseType;
    float memColorIntensity;
    float3 memGravity;
    float memDrag;
    float3 memWind;
    float memTurbulence;
};

RWStructuredBuffer<Particle> particles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);
ByteAddressBuffer emitterMesh : register(t0);

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

    // ===================================
    // : カーブ（イージング）データ
    // ===================================
    uint sizeEaseType;
    uint colorEaseType;
    uint meshVertexCount; // 頂点の総数
    uint meshVertexStride; // 1頂点あたりのバイト数 (例: sizeof(Vertex))
    row_major matrix emitterWorldMatrix;
    
    row_major matrix viewProj;
    row_major matrix inverseViewProj;
    float2 screenSize;
    uint enableCollision;
    float restitution;
    float colorIntensity;
    uint currentConfigIndex;
    float2 padding_col;
};
struct BoneData
{
    row_major matrix finalMatrix;
};
StructuredBuffer<BoneData> boneMatrices : register(t1);
Texture2D<float> depthTex : register(t2);
SamplerState smp : register(s0);
#define PI 3.14159265359f

float EaseInSine(float t)
{
    return 1.0f - cos((t * PI) / 2.0f);
}
float EaseOutSine(float t)
{
    return sin((t * PI) / 2.0f);
}
float EaseInOutSine(float t)
{
    return -(cos(PI * t) - 1.0f) / 2.0f;
}

float EaseInQuad(float t)
{
    return t * t;
}
float EaseOutQuad(float t)
{
    return 1.0f - (1.0f - t) * (1.0f - t);
}
float EaseInOutQuad(float t)
{
    return t < 0.5f ? 2.0f * t * t : 1.0f - pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

float EaseInCubic(float t)
{
    return t * t * t;
}
float EaseOutCubic(float t)
{
    return 1.0f - pow(1.0f - t, 3.0f);
}
float EaseInOutCubic(float t)
{
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float EaseInQuart(float t)
{
    return t * t * t * t;
}
float EaseOutQuart(float t)
{
    return 1.0f - pow(1.0f - t, 4.0f);
}
float EaseInOutQuart(float t)
{
    return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

float EaseInQuint(float t)
{
    return t * t * t * t * t;
}
float EaseOutQuint(float t)
{
    return 1.0f - pow(1.0f - t, 5.0f);
}
float EaseInOutQuint(float t)
{
    return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

float EaseInExpo(float t)
{
    return t == 0.0f ? 0.0f : pow(2.0f, 10.0f * t - 10.0f);
}
float EaseOutExpo(float t)
{
    return t == 1.0f ? 1.0f : 1.0f - pow(2.0f, -10.0f * t);
}
float EaseInOutExpo(float t)
{
    if (t == 0.0f)
        return 0.0f;
    if (t == 1.0f)
        return 1.0f;
    t *= 2.0f;
    if (t < 1.0f)
        return pow(2.0f, 10.0f * (t - 1.0f)) / 2.0f;
    return (2.0f - pow(2.0f, -10.0f * (t - 1.0f))) / 2.0f;
}

float EaseInCirc(float t)
{
    return 1.0f - sqrt(1.0f - pow(t, 2.0f));
}
float EaseOutCirc(float t)
{
    return sqrt(1.0f - pow(t - 1.0f, 2.0f));
}
float EaseInOutCirc(float t)
{
    return t < 0.5f ? (1.0f - sqrt(1.0f - pow(2.0f * t, 2.0f))) / 2.0f : (sqrt(1.0f - pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

float EaseInBack(float t)
{
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}
float EaseOutBack(float t)
{
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    return 1.0f + c3 * pow(t - 1.0f, 3.0f) + c1 * pow(t - 1.0f, 2.0f);
}
float EaseInOutBack(float t)
{
    float c1 = 1.70158f;
    float c2 = c1 * 1.525f;
    return t < 0.5f ? (pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f : (pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (2.0f * t - 2.0f) + c2) + 2.0f) / 2.0f;
}

float EaseInElastic(float t)
{
    float c4 = (2.0f * PI) / 3.0f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : -pow(2.0f, 10.0f * t - 10.0f) * sin((t * 10.0f - 10.75f) * c4);
}
float EaseOutElastic(float t)
{
    float c4 = (2.0f * PI) / 3.0f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : pow(2.0f, -10.0f * t) * sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
float EaseInOutElastic(float t)
{
    float c5 = (2.0f * PI) / 4.5f;
    return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : t < 0.5f ? -(pow(2.0f, 20.0f * t - 10.0f) * sin((20.0f * t - 11.125f) * c5)) / 2.0f : (pow(2.0f, -20.0f * t + 10.0f) * sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
}

float EaseOutBounce(float t)
{
    float n1 = 7.5625f;
    float d1 = 2.75f;
    if (t < 1.0f / d1)
    {
        return n1 * t * t;
    }
    else if (t < 2.0f / d1)
    {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    }
    else if (t < 2.5f / d1)
    {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    else
    {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}
float EaseInBounce(float t)
{
    return 1.0f - EaseOutBounce(1.0f - t);
}
float EaseInOutBounce(float t)
{
    return t < 0.5f ? (1.0f - EaseOutBounce(1.0f - 2.0f * t)) / 2.0f : (1.0f + EaseOutBounce(2.0f * t - 1.0f)) / 2.0f;
}

// 指定された番号のイージングを適用する統合関数
float ApplyEasing(uint type, float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    switch (type)
    {
        case 0:
            return t;
        case 1:
            return EaseInSine(t);
        case 2:
            return EaseOutSine(t);
        case 3:
            return EaseInOutSine(t);
        case 4:
            return EaseInQuad(t);
        case 5:
            return EaseOutQuad(t);
        case 6:
            return EaseInOutQuad(t);
        case 7:
            return EaseInCubic(t);
        case 8:
            return EaseOutCubic(t);
        case 9:
            return EaseInOutCubic(t);
        case 10:
            return EaseInQuart(t);
        case 11:
            return EaseOutQuart(t);
        case 12:
            return EaseInOutQuart(t);
        case 13:
            return EaseInQuint(t);
        case 14:
            return EaseOutQuint(t);
        case 15:
            return EaseInOutQuint(t);
        case 16:
            return EaseInExpo(t);
        case 17:
            return EaseOutExpo(t);
        case 18:
            return EaseInOutExpo(t);
        case 19:
            return EaseInCirc(t);
        case 20:
            return EaseOutCirc(t);
        case 21:
            return EaseInOutCirc(t);
        case 22:
            return EaseInBack(t);
        case 23:
            return EaseOutBack(t);
        case 24:
            return EaseInOutBack(t);
        case 25:
            return EaseInElastic(t);
        case 26:
            return EaseOutElastic(t);
        case 27:
            return EaseInOutElastic(t);
        case 28:
            return EaseInBounce(t);
        case 29:
            return EaseOutBounce(t);
        case 30:
            return EaseInOutBounce(t);
    }
    return t;
}


// HLSLで超高速に乱数を生成する魔法の関数
float rand(float2 seed)
{
    return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}
float3 GetWorldPosFromDepth(float2 uv, float depth, matrix invViewProj)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 worldPos = mul(ndc, invViewProj);
    return worldPos.xyz / worldPos.w;
}
[numthreads(1024, 1, 1)]
void InitCS(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    if (index < 10000)
    {
        Particle p = (Particle)0;
        particles[index] = p;
        gFreeList[index] = index;
    }
    if (index == 0)
    {
        gFreeListIndex[0] = 10000 - 1;
    }
}

[numthreads(256, 1, 1)]
void EmitCS(uint3 DTid : SV_DispatchThreadID)
{
    uint threadIndex = DTid.x;
    if (threadIndex >= emitCount)
        return;

    int freeListPos;
    InterlockedAdd(gFreeListIndex[0], -1, freeListPos);

    if (freeListPos >= 0)
    {
        uint pIndex = gFreeList[freeListPos];
        Particle p = particles[pIndex];

        // 共通で使う乱数
        float rX = rand(float2(pIndex * 1.34f, time)) * 2.0f - 1.0f;
        float rY = rand(float2(time * 1.57f, pIndex)) * 2.0f - 1.0f;
        float rZ = rand(float2(pIndex * 1.89f, time * 2.13f)) * 2.0f - 1.0f;
        float r1 = rand(float2(pIndex + 1.0f, time * 1.1f)) * 2.0f - 1.0f;
        float r2 = rand(float2(time + 1.2f, pIndex * 1.3f)) * 2.0f - 1.0f;
        float r3 = rand(float2(pIndex * time + 1.4f, 1.5f)) * 2.0f - 1.0f;

        p.life = emitLife;
        p.maxLife = emitLife;

        // ========================================================
        // ★ 形状ごとの発生アルゴリズム
        // ========================================================
        if (shapeType == 1) // 🟢 Sphere (球体)
        {
            float3 dir = normalize(float3(rX, rY, rZ));
            float dist = rand(float2(pIndex, time * 0.5f)) * shapeRadius;
            p.position = emitPos + dir * dist;
            p.velocity = emitVelocity + (dir * velocityVariance);
        }
        else if (shapeType == 2) // 🔺 Cone (円錐)
        {
            float theta = rand(float2(time, pIndex * 2.1f)) * 6.28318f;
            float rad = sqrt(rand(float2(pIndex, time * 3.4f)));
            float spread = tan(radians(shapeAngle));
            float2 localCircle = float2(cos(theta), sin(theta)) * rad;
            float3 localDir = normalize(float3(localCircle.x * spread, localCircle.y * spread, 1.0f));

            float3 forward = length(emitVelocity) > 0.001f ? normalize(emitVelocity) : float3(0, 1, 0);
            float3 right = abs(forward.y) < 0.999f ? normalize(cross(float3(0, 1, 0), forward)) : float3(1, 0, 0);
            float3 upAxis = cross(forward, right);

            float3 worldDir = localDir.x * right + localDir.y * upAxis + localDir.z * forward;
            p.position = emitPos + (localCircle.x * right + localCircle.y * upAxis) * shapeRadius;

            float speed = length(emitVelocity) + r1 * velocityVariance;
            p.velocity = worldDir * speed;
        }
        else if (shapeType == 3) // 🔷 Mesh (3Dモデルの表面)
        {
            uint vIndex = (uint) (rand(float2(pIndex, time)) * meshVertexCount) % max(meshVertexCount, 1);
            uint byteOffset = vIndex * meshVertexStride;
            
            float3 localPos = asfloat(emitterMesh.Load3(byteOffset));
            float4 weights = asfloat(emitterMesh.Load4(byteOffset + 48));
            float4 indices = asfloat(emitterMesh.Load4(byteOffset + 64));
            
            matrix boneMat =
                boneMatrices[(int) indices.x].finalMatrix * weights.x +
                boneMatrices[(int) indices.y].finalMatrix * weights.y +
                boneMatrices[(int) indices.z].finalMatrix * weights.z +
                boneMatrices[(int) indices.w].finalMatrix * weights.w;
                
            float3 skinnedPos = mul(float4(localPos, 1.0f), boneMat).xyz;
            float3 worldPos = mul(float4(skinnedPos, 1.0f), emitterWorldMatrix).xyz;
            
            p.position = worldPos;
            p.velocity = emitVelocity + float3(r1, r2, r3) * velocityVariance;
        }
        else if (shapeType == 4) // ハート
        {
            float theta = rand(float2(pIndex, time)) * 6.28318f;
            float hx = 16.0f * pow(sin(theta), 3.0f);
            float hy = 13.0f * cos(theta) - 5.0f * cos(2.0f * theta) - 2.0f * cos(3.0f * theta) - cos(4.0f * theta);
            
            hx *= 0.05f * shapeRadius;
            hy *= 0.05f * shapeRadius;
            
            float thickness = emitArea.x;
            float lineThickness = emitArea.y;
            
            float noiseX = rX * lineThickness;
            float noiseY = rY * lineThickness;
            float noiseZ = rZ * thickness;
            
            p.position = emitPos + float3(hx + noiseX, hy + noiseY, noiseZ);
            p.velocity = emitVelocity + float3(r1, r2, r3) * velocityVariance;
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
        
        p.rotation = rand(float2(pIndex, time)) * 6.28318f;
        p.rotSpeed = (rand(float2(time, pIndex)) * 2.0f - 1.0f) * rotSpeedVariance;
        p.scale = baseSize;
        p.memBaseColor = baseColor;
        p.memMidColor = midColor;
        p.memEndColor = endColor;
        p.memBaseSize = baseSize;
        p.memMidSize = midSize;
        p.memEndSize = endSize;
        p.memColorMidTime = colorMidTime;
        p.memSizeMidTime = sizeMidTime;
        p.memColorEaseType = colorEaseType;
        p.memSizeEaseType = sizeEaseType;
        p.memColorIntensity = colorIntensity;
        p.memGravity = gravity;
        p.memDrag = drag;
        p.memWind = wind;
        p.memTurbulence = turbulence;

        particles[pIndex] = p;
    }
    else
    {
        // 取れるパーティクルが無かった場合は、デクリメントをキャンセル（元に戻す）
        InterlockedAdd(gFreeListIndex[0], 1);
    }
}

[numthreads(256, 1, 1)]
void UpdateCS(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;

    if (index >= 10000)
        return;

    Particle p = particles[index];

    if (p.life > 0.0f)
    {
        p.life -= deltaTime;
        
        p.velocity += p.memGravity * deltaTime;
        p.velocity += p.memWind * deltaTime;
        
        float3 noiseVec = float3(
            rand(p.position.xy + time) * 2.0f - 1.0f,
            rand(p.position.yz - time) * 2.0f - 1.0f,
            rand(p.position.zx + time) * 2.0f - 1.0f
        );
        p.rotation += p.rotSpeed * deltaTime;
        p.velocity += noiseVec * p.memTurbulence * deltaTime;
        
        p.velocity *= saturate(1.0f - p.memDrag * deltaTime);
        p.position += p.velocity * deltaTime;

        // 深度バッファ・コリジョン
        if (enableCollision > 0)
        {
            float4 clipPos = mul(float4(p.position, 1.0f), viewProj);
            float3 ndcPos = clipPos.xyz / clipPos.w;
            float2 uv = ndcPos.xy * float2(0.5f, -0.5f) + 0.5f;

            if (uv.x > 0.0f && uv.x < 1.0f && uv.y > 0.0f && uv.y < 1.0f && ndcPos.z < 1.0f)
            {
                float bgDepth = depthTex.SampleLevel(smp, uv, 0);
                
                if (ndcPos.z >= bgDepth && ndcPos.z < bgDepth + 0.05f && bgDepth < 1.0f)
                {
                    p.position -= p.velocity * deltaTime;

                    float2 offset = 1.0f / screenSize;
                    float depthX = depthTex.SampleLevel(smp, uv + float2(offset.x, 0), 0);
                    float depthY = depthTex.SampleLevel(smp, uv + float2(0, offset.y), 0);

                    float3 p0 = GetWorldPosFromDepth(uv, bgDepth, inverseViewProj);
                    float3 p1 = GetWorldPosFromDepth(uv + float2(offset.x, 0), depthX, inverseViewProj);
                    float3 p2 = GetWorldPosFromDepth(uv + float2(0, offset.y), depthY, inverseViewProj);

                    float3 normal = normalize(cross(p2 - p0, p1 - p0));

                    if (dot(normal, p.velocity) > 0.0f)
                    {
                        normal = -normal;
                    }

                    p.velocity = reflect(p.velocity, normal) * restitution;
                    p.velocity.xz *= 0.8f;
                    p.position += p.velocity * deltaTime;
                }
            }
        }

        float ageRatio = saturate(1.0f - (p.life / p.maxLife));
        
        // 1. サイズのイージングカーブ適用
        float sizeRatio = ApplyEasing(p.memSizeEaseType, ageRatio);
        if (sizeRatio < p.memSizeMidTime)
        {
            float t = sizeRatio / max(p.memSizeMidTime, 0.001f);
            p.scale = lerp(p.memBaseSize, p.memMidSize, t);
        }
        else
        {
            float t = (sizeRatio - p.memSizeMidTime) / max(1.0f - p.memSizeMidTime, 0.001f);
            p.scale = lerp(p.memMidSize, p.memEndSize, t);
        }
        
        // 2. カラー＆透明度（Alpha）のイージングカーブ適用
        float colorRatio = ApplyEasing(p.memColorEaseType, ageRatio);
        if (colorRatio < p.memColorMidTime)
        {
            float t = colorRatio / max(p.memColorMidTime, 0.001f);
            p.color = lerp(p.memBaseColor, p.memMidColor, t);
        }
        else
        {
            float t = (colorRatio - p.memColorMidTime) / max(1.0f - p.memColorMidTime, 0.001f);
            p.color = lerp(p.memMidColor, p.memEndColor, t);
        }
        p.color.rgb *= p.memColorIntensity;

        if (p.life <= 0.0f)
        {
            // Particle just died! Return to FreeList
            int freeListPos;
            InterlockedAdd(gFreeListIndex[0], 1, freeListPos);
            if (freeListPos + 1 < 10000)
            {
                gFreeList[freeListPos + 1] = index;
            }
        }

        particles[index] = p;
    }
}