// [新規作成] DebugPrimitive.hlsli

struct VSInput
{
    float4 position : POSITION0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

// オブジェクトごとの変換行列
cbuffer ObjectTransform : register(b0)
{
    matrix worldViewProjection; // ワールド x ビュー x プロジェクション
};

// フレーム全体で共通の色
cbuffer ObjectColor : register(b1)
{
    float4 color; // 描画色
};