// ==========================================================
// GPUParticlePS.hlsl : GPUパーティクル描画用 ピクセルシェーダー
// ==========================================================

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

// キラキラ光る画像などをセットする
Texture2D tex : register(t1);
SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET
{
    // 画像の色を取得
    float4 texColor = tex.Sample(smp, input.uv);
    
    // パーティクル自体の色（フェードアウトの透明度含む）と掛け合わせる
    float4 finalColor = input.color * texColor;
    
    // 透明な部分は描画しない
    if (finalColor.a <= 0.0f)
    {
        discard;
    }
    
    return finalColor;
}