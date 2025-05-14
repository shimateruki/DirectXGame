
struct VertexShanderOutput
{
    float32_t4 position : SV_POSITION;
};

struct VertexShanderInput
{
    float32_t4 position :POSITION0;
};

VertexShanderOutput main(VertexShanderInput input)
{
    VertexShanderOutput output;
    output.position = input.position;
    return output;
}
