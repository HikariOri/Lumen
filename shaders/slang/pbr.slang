// pbr.slang

// 输入结构体：顶点着色器传来的内容
struct VSInput {
    float3 position    : POSITION;
    float3 normal      : NORMAL;
    float2 uv          : TEXCOORD0;
    float3 tangent     : TANGENT;    // 如果你使用切线空间法线贴图
};

// 如果你需要法线空间里切线空间法线贴图变换
struct VSOutput {
    float4 position    : SV_POSITION;
    float3 worldPos    : TEXCOORD0;
    float3 normal      : TEXCOORD1;
    float3 tangent     : TEXCOORD2;
    float2 uv          : TEXCOORD3;
};

// 材质参数
struct Material {
    float4 baseColor;        // (r,g,b,a)
    float metallic;          // 0 = 绝缘体, 1 = 金属
    float roughness;         // 粗糙度
    float ao;                // 环境光遮蔽
    Texture2D baseColorMap;
    Texture2D normalMap;
    Texture2D metallicRoughnessMap; // 通常 metal 在 R 或 B 通道, roughness 在 G 通道
    SamplerState linearSampler;
};

// 光照结构体
struct Light {
    float3 direction;        // 对方向光
    float3 color;            // 光颜色
};

// 常用环境／IBL（简化版）
TextureCube<float4> envMap;
SamplerState envSampler;

// 常量缓冲区
cbuffer CameraConsts {
    float4x4 worldMatrix;
    float4x4 viewProjMatrix;
    float3   cameraPosition;
};

// 计算半程向量（用于镜面反射）
float3 fresnelSchlick(float3 F0, float cosine) {
    return F0 + (1.0f - F0) * pow(1.0f - cosine, 5.0f);
}

// 主 VS 着色器
[shader("vertex")]
VSOutput vs_main(VSInput input) {
    VSOutput o;
    float4 worldPos4 = mul(worldMatrix, float4(input.position, 1.0f));
    o.worldPos = worldPos4.xyz;
    o.position = mul(viewProjMatrix, float4(input.position, 1.0f));
    
    // 法线、切线变换到世界空间
    o.normal = normalize(mul((float3x3)worldMatrix, input.normal));
    o.tangent = normalize(mul((float3x3)worldMatrix, input.tangent));
    o.uv = input.uv;
    return o;
}

// 主像素着色器
[shader("fragment")]
float4 ps_main(VSOutput in : TEXCOORD3, Material mat, Light light) : SV_TARGET {
    // 基础属性采样
    float4 baseColorSample = mat.baseColor;
    baseColorSample *= mat.baseColorMap.Sample(mat.linearSampler, in.uv);

    float metallic = mat.metallic;
    float roughness = mat.roughness;
    float ao = mat.ao;

    // 如果用金属粗糙度图
    #if defined(USE_METALLIC_ROUGHNESS_MAP)
        float4 mr = mat.metallicRoughnessMap.Sample(mat.linearSampler, in.uv);
        metallic = mr.r;
        roughness = mr.g;
    #endif

    // 法线贴图
    float3 N = in.normal;
    #if defined(USE_NORMAL_MAP)
        float3 normalMapSample = mat.normalMap.Sample(mat.linearSampler, in.uv).xyz * 2.0f - 1.0f;
        // 用切线-法线空间转换 (假设 TBN 已构造)
        float3 T = normalize(in.tangent);
        float3 B = normalize(cross(N, T));
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(TBN, normalMapSample));
    #endif

    // 视线向量
    float3 V = normalize(cameraPosition - in.worldPos);

    // 光线向量 (方向光简化)
    float3 L = normalize(-light.direction); // 假设 light.direction 指从光源发出方向

    // 中间变量
    float3 H = normalize(V + L);

    // 法线-光照角度
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);

    // Fresnel
    float3 F0 = float3(0.04f, 0.04f, 0.04f); // 对非金属
    F0 = lerp(F0, baseColorSample.rgb, metallic);
    float3 F = fresnelSchlick(F0, max(dot(H, V), 0.0f));

    // 法线分布函数 (NDF) — 示例用 GGX / Trowbridge-Reitz
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = (dot(N, H) * dot(N, H)) * (alpha2 - 1.0f) + 1.0f;
    float D = alpha2 / (PI * denom * denom + 1e-7f);

    // 几何遮蔽函数（Schlick-GGX + Smith）
    float k = (alpha + 1.0f) * (alpha + 1.0f) / 8.0f;
    float G_V = NdotV / (NdotV * (1.0f - k) + k + 1e-7f);
    float G_L = NdotL / (NdotL * (1.0f - k) + k + 1e-7f);
    float G = G_V * G_L;

    float3 numerator = D * G * F;
    float3 denominator = 4.0f * NdotV * NdotL + 1e-7f;
    float3 specular = numerator / denominator;

    // 漫反射 (Lambert)
    float3 kD = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = baseColorSample.rgb * (1.0f / PI);

    // 环境光 + 环境贴图（简化）
    float3 ambient = ao * baseColorSample.rgb * 0.03f;
    // 如果有环境贴图，则：
    float3 reflection = envMap.Sample(envSampler, reflect(-V, N)).rgb;
    ambient += reflection * F * 0.5f; // 环境反射权重

    float3 color = ambient + (diffuse + specular) * light.color * NdotL;

    return float4(color, baseColorSample.a);
}
