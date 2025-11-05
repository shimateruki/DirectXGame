// C++側から送られてくる、各パーティクルの情報
struct ParticleForVS
{
    float4 color : COLOR0;
    matrix world : WORLD0; // WORLD0-WORLD3 の4行を使う
};

// 頂点シェーダーへの入力
struct VSInput
{
    float4 position : POSITION0; // 基本となる四角形の頂点座標 (-0.5, -0.5) など
    float2 texcoord : TEXCOORD0;
};

// ピクセルシェーダーへの出力
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

// 定数バッファ
cbuffer CameraMatrix : register(b0)
{
    matrix viewProjectionMatrix;
}

// 頂点シェーダー
VSOutput main(VSInput input, ParticleForVS instance)
{
    VSOutput output;
    // パーティクルのワールド行列と、基本頂点座標を合成し、さらにビュープロジェクション行列を適用
    output.position = mul(input.position, instance.world);
    output.position = mul(output.position, viewProjectionMatrix);
    
    output.texcoord = input.texcoord;
    output.color = instance.color;
    return output;
}