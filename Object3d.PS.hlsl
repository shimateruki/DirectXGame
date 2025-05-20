
struct PixelShanderOutput
{
    float32_t4 color : SV_TARGET0;
    
};
struct Material
{
    float32_t4 color;
};

ConstantBuffer<Material> gMaterial: register(b0);

struct pixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};



PixelShanderOutput main()
{
    PixelShanderOutput output;
    
        output.color = gMaterial.color;
    return output;
}