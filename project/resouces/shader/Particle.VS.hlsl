// C++�����瑗���Ă���A�e�p�[�e�B�N���̏��
struct ParticleForVS
{
    float4 color : COLOR0;
    matrix world : WORLD0; // WORLD0-WORLD3 ��4�s���g��
};

// ���_�V�F�[�_�[�ւ̓���
struct VSInput
{
    float4 position : POSITION0; // ��{�ƂȂ�l�p�`�̒��_���W (-0.5, -0.5) �Ȃ�
    float2 texcoord : TEXCOORD0;
};

// �s�N�Z���V�F�[�_�[�ւ̏o��
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

// �萔�o�b�t�@
cbuffer CameraMatrix : register(b0)
{
    matrix viewProjectionMatrix;
}

// ���_�V�F�[�_�[
VSOutput main(VSInput input, ParticleForVS instance)
{
    VSOutput output;
    // �p�[�e�B�N���̃��[���h�s��ƁA��{���_���W���������A����Ƀr���[�v���W�F�N�V�����s���K�p
    output.position = mul(input.position, instance.world);
    output.position = mul(output.position, viewProjectionMatrix);
    
    output.texcoord = input.texcoord;
    output.color = instance.color;
    return output;
}