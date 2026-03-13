// ==========================================
// LocalFog.VS.hlsl (ローカルフォグ用 頂点シェーダー)
// ==========================================

struct VSInput
{
    float4 position : POSITION0;
};

struct VSOutput
{
    float4 svpos : SV_POSITION;
    float4 projPos : TEXCOORD0; // 画面のUV座標を計算するための座標
    float3 worldPos : TEXCOORD1; // 箱の表面のワールド座標
    float3 localPos : TEXCOORD2;
};

// C++側の TransformationMatrix と合わせる
cbuffer Transform : register(b0)
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    // 画面上の座標に変換
    output.svpos = mul(input.position, WVP);
    
    // プロジェクション後の座標をそのままピクセルシェーダーへ送る
    output.projPos = output.svpos;
    
    // 箱の表面のワールド座標を計算
    output.worldPos = mul(input.position, World).xyz;
    output.localPos = input.position.xyz;
    
    return output;
}