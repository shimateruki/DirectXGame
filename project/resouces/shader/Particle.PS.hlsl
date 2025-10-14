// ���_�V�F�[�_�[����󂯎��f�[�^
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// �s�N�Z���V�F�[�_�[
float4 main(VSOutput input) : SV_TARGET
{
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    return textureColor * input.color;
}