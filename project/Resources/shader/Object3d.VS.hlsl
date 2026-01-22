#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

// スキニング用の行列パレット (StructuredBuffer, register t1)
StructuredBuffer<float32_t4x4> gMatrixPalette : register(t1);

// ※構造体名は元のまま維持しています
struct VertexShanderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t4 weight : WEIGHT0;
    float32_t4 index : INDEX0;
};

// ※戻り値の型名も元のまま維持しています
VecrtexShaderOutput main(VertexShanderInput input)
{
    VecrtexShaderOutput output;

    // ★重要: floatで受け取ったインデックスを、整数(int)にキャストします
    // これをしないと、配列の要素番号として正しくアクセスできません
    int4 iIndex = int4(input.index);

    // =========================================================
    // ★スキニング行列の計算 (Linear Blend Skinning)
    // =========================================================
    // 4つのボーンの影響を重み(weight)に応じて足し合わせる
    float32_t4x4 skinningMatrix =
        gMatrixPalette[iIndex.x] * input.weight.x +
        gMatrixPalette[iIndex.y] * input.weight.y +
        gMatrixPalette[iIndex.z] * input.weight.z +
        gMatrixPalette[iIndex.w] * input.weight.w;

    // =========================================================
    // ★頂点と法線の変換
    // =========================================================
    // 入力頂点(Local)にスキニング行列をかけて変形させる
    float32_t4 skinnedPosition = mul(input.position, skinningMatrix);
    
    // 法線も回転させる (平行移動成分は不要なので3x3キャスト)
    float32_t3 skinnedNormal = mul(input.normal, (float32_t3x3) skinningMatrix);

    // ★追加: 強制スムース法線の計算 (Sphere Cheat)
    // 球体専用: ローカル座標自体を法線とみなすことで、ポリゴンの角を無視して滑らかにする
    float32_t3 localSmoothNormal = normalize(input.position.xyz);
    // スムース法線にもスキニングを適用
    float32_t3 skinnedSmoothNormal = mul(localSmoothNormal, (float32_t3x3) skinningMatrix);


    // =========================================================
    // 通常の座標変換 (変形後の頂点に対して行う)
    // =========================================================
    
    // WVP変換 (Local -> World -> View -> Proj)
    // input.position ではなく skinnedPosition を使う！
    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);
    
    output.texcoord = input.texcoord;
    
    // 法線のワールド変換
    // input.normal ではなく skinnedNormal を使う！
    output.normal = normalize(mul(skinnedNormal, (float32_t3x3) gTransformationMatrix.WorldInverseTranspose));
    
    //  スムース法線の出力
    // ガラス描画用 (hlsliに smoothNormal を追加している前提)
    output.smoothNormal = normalize(mul(skinnedSmoothNormal, (float32_t3x3) gTransformationMatrix.World));

    // ワールド座標 (PixelShader用)
    // input.position ではなく skinnedPosition を使う！
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    

    return output;
}