// ==========================================================
// GPUParticleVS.hlsl : GPUパーティクル描画用 頂点シェーダー
// ==========================================================

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

StructuredBuffer<Particle> particles : register(t0);
StructuredBuffer<uint> aliveParticleIndices : register(t4);

cbuffer ViewProj : register(b0)
{
    row_major matrix viewProj;
    row_major matrix billboardMatrix;
    // ★追加: PSと構造を合わせるため projection も追加しておく
    row_major matrix projection;
    float softParticleFade;
    int blendMode;
    float2 screenSize;
    uint spriteSheetColumns;
    uint spriteSheetRows;
    uint spriteSheetFrameCount;
    float spriteSheetFps;
    uint spriteSheetLoop;
    uint spriteSheetRandomStart;
    uint alignToVelocity;
    float velocityStretch;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 projPos : TEXCOORD1; // ★追加：ソフトパーティクル用
};

static const float3 positions[4] =
{
    float3(-0.5f, -0.5f, 0.0f),
    float3(-0.5f, 0.5f, 0.0f),
    float3(0.5f, -0.5f, 0.0f),
    float3(0.5f, 0.5f, 0.0f)
};

static const float2 uvs[4] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 1.0f),
    float2(1.0f, 0.0f)
};

float2 ApplySpriteSheet(float2 baseUV, Particle particle, uint instanceID)
{
    uint columns = max(spriteSheetColumns, uint(1));
    uint rows = max(spriteSheetRows, uint(1));
    uint capacity = max(columns * rows, uint(1));
    uint frameCount = min(max(spriteSheetFrameCount, uint(1)), capacity);
    uint frame = uint(0);

    if (frameCount > uint(1) && spriteSheetFps > 0.0f)
    {
        float age = max(0.0f, particle.maxLife - particle.life);
        float randomOffset = 0.0f;
        if (spriteSheetRandomStart != uint(0))
        {
            float seed = (float)instanceID * 12.9898f + particle.maxLife * 78.233f;
            randomOffset = floor(frac(sin(seed) * 43758.5453f) * (float)frameCount);
        }

        uint rawFrame = (uint)floor(age * spriteSheetFps + randomOffset);
        frame = (spriteSheetLoop != uint(0)) ? (rawFrame % frameCount) : min(rawFrame, frameCount - uint(1));
    }

    uint frameX = frame % columns;
    uint frameY = frame / columns;
    float2 frameScale = float2(1.0f / (float)columns, 1.0f / (float)rows);
    // Keep bilinear sampling inside the selected cell to prevent atlas bleeding.
    float2 safeUV = lerp(float2(0.004f, 0.004f), float2(0.996f, 0.996f), saturate(baseUV));
    return (safeUV + float2((float)frameX, (float)frameY)) * frameScale;
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    uint particleIndex = aliveParticleIndices[instanceID];
    Particle p = particles[particleIndex];

    float3 localPos = positions[vertexID] * p.scale;
    float velocityLength = length(p.velocity);
    localPos.x *= 1.0f + min(velocityLength * max(velocityStretch, 0.0f), 2.5f);

    float rotation = p.rotation;
    if (alignToVelocity != uint(0) && velocityLength > 0.0001f)
    {
        float3 cameraRight = normalize(float3(billboardMatrix._11, billboardMatrix._12, billboardMatrix._13));
        float3 cameraUp = normalize(float3(billboardMatrix._21, billboardMatrix._22, billboardMatrix._23));
        float2 projectedVelocity = float2(dot(p.velocity, cameraRight), dot(p.velocity, cameraUp));
        if (dot(projectedVelocity, projectedVelocity) > 0.000001f)
        {
            rotation += atan2(projectedVelocity.y, projectedVelocity.x);
        }
    }

    float c = cos(rotation);
    float s = sin(rotation);
    float3 rotatedPos;
    rotatedPos.x = localPos.x * c - localPos.y * s;
    rotatedPos.y = localPos.x * s + localPos.y * c;
    rotatedPos.z = localPos.z;

    float3 worldPos = p.position + mul(rotatedPos, (float3x3) billboardMatrix);

    output.pos = mul(float4(worldPos, 1.0f), viewProj);
    output.uv = ApplySpriteSheet(uvs[vertexID], p, particleIndex);
    output.color = p.color;
    
    // =======================================================
    // ：これが無いとPSで計算できず透明になって消えます！
    // =======================================================
    output.projPos = output.pos;
    
    return output;
}
