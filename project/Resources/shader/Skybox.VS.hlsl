// Skybox.VS.hlsl
#include "Skybox.hlsli"

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // 【安全な移動成分の消去】
    // view行列を3x3行列にキャストすることで、回転成分だけを抽出し平行移動を安全に切り捨てます
    float3x3 view3x3 = (float3x3) view;
    
    // 頂点と3x3ビュー行列を掛け合わせる
    float3 viewPos = mul(input.position.xyz, view3x3);
    
    // その結果にプロジェクション行列を掛けてクリップ空間座標へ変換
    output.position = mul(float4(viewPos, 1.0f), projection);
    
    // 【深度トリック】
    // Zの値をWと同じにすることで、パースペクティブ除算 (Z/W) 後に Z=1.0 (最奥) になる
    output.position.z = output.position.w;
    
    // キューブマップのサンプリングには、頂点のローカル座標(xyz)をそのまま方向ベクトルとして使う
    output.texcoord = input.position.xyz;
    
    return output;
}