// =============================
// Descriptor Sets（保持你的分层）
// set0: frame
// set1: object
// set2: material
// set3: lighting / IBL
// =============================

// ===== Frame =====
struct FrameUBO
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;

    float3 cameraPos;
    float time;

    float2 screenSize;

    float4 exposureIblMips;

    int debugMode;
};

// ===== Object =====
struct ObjectUBO
{
    float4x4 model;
    float4x4 normalMatrix;
};

// ===== Material =====
struct MaterialUBO
{
    float4 baseColorFactor;
    float4 emissive;

    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;

    uint flags;
    uint alphaMode;

    float alphaCutoff;
};

// ====== 资源绑定（保留 set/binding，方便 Vulkan） ======
[[vk::binding(0, 0)]]
ConstantBuffer<FrameUBO> frameUBO;

[[vk::binding(1, 0)]]
ConstantBuffer<ObjectUBO> objectUBO;

[[vk::binding(2, 0)]]
ConstantBuffer<MaterialUBO> materialUBO;

// textures（set2）
[[vk::binding(2, 1)]] Texture2D baseColorTex;
[[vk::binding(2, 2)]] Texture2D metallicRoughnessTex;
[[vk::binding(2, 3)]] Texture2D normalTex;
[[vk::binding(2, 4)]] Texture2D occlusionTex;
[[vk::binding(2, 5)]] Texture2D emissiveTex;

SamplerState samp; // 通用 sampler

// IBL（set3）
[[vk::binding(3, 1)]] TextureCube irradianceMap;
[[vk::binding(3, 2)]] TextureCube prefilteredMap;
[[vk::binding(3, 3)]] Texture2D brdfLUT;


// =============================
// VS → FS 传递结构（核心）
// =============================
struct VSOutput
{
    float4 position : SV_Position;
    float3 color    : TEXCOORD0;
};


// =============================
// Vertex Shader
// =============================
[shader("vertex")]
VSOutput vsMain(
    float2 inPos   : POSITION,
    float3 inColor : COLOR
)
{
    VSOutput o;

    float4 worldPos = mul(objectUBO.model, float4(inPos, 0.0, 1.0));

    o.position = worldPos;
    o.color = inColor;

    return o;
}


// =============================
// Fragment Shader
// =============================
[shader("fragment")]
float4 psMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
