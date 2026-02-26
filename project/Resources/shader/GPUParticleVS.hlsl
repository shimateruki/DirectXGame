// ==========================================================
// GPUParticleVS.hlsl : GPUパーティクル描画用 頂点シェーダー
// ==========================================================

// C++側の構造体と合わせる
struct Particle
{
    float3 position;
    float life;
    float3 velocity;
    float maxLife;
    float4 color;
};

// Compute Shaderで計算した結果を "読み取り専用(t0)" として受け取る！
StructuredBuffer<Particle> particles : register(t0);

cbuffer ViewProj : register(b0)
{
    row_major matrix viewProj;
    row_major matrix billboardMatrix;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

// 1枚の四角形を作るための4つの頂点座標
static const float3 positions[4] =
{
    float3(-0.5f, -0.5f, 0.0f), // 左下
    float3(-0.5f, 0.5f, 0.0f), // 左上
    float3(0.5f, -0.5f, 0.0f), // 右下
    float3(0.5f, 0.5f, 0.0f) // 右上
};

static const float2 uvs[4] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 1.0f),
    float2(1.0f, 0.0f)
};

// SV_VertexID: 四角形の何番目のカドか (0~3)
// SV_InstanceID: 何番目のパーティクルか (0~99999)
VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    
    // 自分の番号のパーティクル情報を取得！
    Particle p = particles[instanceID];
    
    // もし死んでいたら、画面外にすっ飛ばして描画させない（不可視にする）
    if (p.life <= 0.0f)
    {
        output.pos = float4(0, 0, 0, 0);
        output.color = float4(0, 0, 0, 0);
        output.uv = float2(0.0f, 0.0f); 
        return output;
    }
    // パーティクルのサイズ (とりあえず1.0f)
    float particleSize = 1.0f;
    float3 localPos = positions[vertexID] * particleSize;
    
    // ビルボード（常にカメラの方を向かせる処理）
    // localPos を billboardMatrix で回転させる
    float3 worldPos = p.position + mul(localPos, (float3x3) billboardMatrix);
    
    // 最終的な画面上の座標に変換
    output.pos = mul(float4(worldPos, 1.0f), viewProj);
    output.uv = uvs[vertexID];
    output.color = p.color;
    
    return output;
}