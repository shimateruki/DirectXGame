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
    float padding;
};

StructuredBuffer<Particle> particles : register(t0);

cbuffer ViewProj : register(b0)
{
    row_major matrix viewProj;
    row_major matrix billboardMatrix;
    // ★追加: PSと構造を合わせるため projection も追加しておく
    row_major matrix projection;
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

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    Particle p = particles[instanceID];
    
    if (p.life <= 0.0f)
    {
        output.pos = float4(0, 0, 0, 0);
        output.color = float4(0, 0, 0, 0);
        output.uv = float2(0.0f, 0.0f);
        output.projPos = float4(0, 0, 0, 0);
        return output;
    }

    float3 localPos = positions[vertexID] * p.scale;
    float c = cos(p.rotation);
    float s = sin(p.rotation);
    float3 rotatedPos;
    rotatedPos.x = localPos.x * c - localPos.y * s;
    rotatedPos.y = localPos.x * s + localPos.y * c;
    rotatedPos.z = localPos.z;

    float3 worldPos = p.position + mul(rotatedPos, (float3x3) billboardMatrix);

    output.pos = mul(float4(worldPos, 1.0f), viewProj);
    output.uv = uvs[vertexID];
    output.color = p.color;
    
    // =======================================================
    // ★修正：これが無いとPSで計算できず透明になって消えます！
    // =======================================================
    output.projPos = output.pos;
    
    return output;
}