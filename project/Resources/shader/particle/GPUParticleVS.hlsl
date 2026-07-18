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
    float2 spriteSheetPadding;
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
    return (baseUV + float2((float)frameX, (float)frameY)) * frameScale;
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    uint particleIndex = aliveParticleIndices[instanceID];
    Particle p = particles[particleIndex];

    float3 localPos = positions[vertexID] * p.scale;
    float c = cos(p.rotation);
    float s = sin(p.rotation);
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
