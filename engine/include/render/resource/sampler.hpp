/**
 * @file sampler.hpp
 * @brief Vulkan VkSampler 封装（纹理采样状态对象）
 *
 * ============================
 * 1. Sampler 在 Vulkan 中的本质
 * ============================
 * VkSampler 是一个“纯状态对象”，它不包含纹理数据，只定义：
 *
 *  - 如何读取纹理（filtering）
 *  - 超出 UV 范围如何处理（addressing mode）
 *  - mipmap 如何选择（LOD selection）
 *  - 是否启用各向异性过滤（anisotropy）
 *
 * Sampler 会在 fragment shader 中通过 descriptor set 绑定：
 *
 *    combined image sampler = ImageView + Sampler
 *
 * @note Sampler 创建成本较高，应尽量复用（不要每个 material 都 new 一个）
 */

#pragma once

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

/**
 * ============================
 * SamplerConfig（创建参数）
 * ============================
 *
 * Vulkan 原生 VkSamplerCreateInfo 的“高层封装”。
 *
 * 设计目标：
 *  - 简化 Vulkan 冗长结构体
 *  - 提供默认 PBR/引擎友好配置
 *  - 便于做 sampler cache（后续扩展）
 */
struct SamplerConfig {

    /**
     * ============================
     * magFilter（放大过滤）
     * ============================
     *
     * 当纹理被“放大显示”（texel < pixel）时的采样方式
     *
     * VK_FILTER_NEAREST：
     *   - 最近邻
     *   - 锯齿明显
     *   - 快（几乎不做插值）
     *
     * VK_FILTER_LINEAR：
     *   - 双线性过滤
     *   - 平滑过渡（默认推荐）
     *
     * GPU层行为：
     *   - 读取 2~4 texels 做插值
     */
    VkFilter magFilter { VK_FILTER_LINEAR };

    /**
     * ============================
     * minFilter（缩小过滤）
     * ============================
     *
     * 当纹理被“缩小显示”（texel > pixel）时的采样方式
     *
     * 常见组合：
     *  - LINEAR + mipmap LINEAR（最常见 PBR）
     *  - NEAREST + mipmap NEAREST（像素风）
     *
     * @note 如果没有 mipmap：
     *   - GPU 仍会采样 base level
     *   - 可能产生闪烁 aliasing
     */
    VkFilter minFilter { VK_FILTER_LINEAR };

    /**
     * ============================
     * mipmapMode（mipmap 选择方式）
     * ============================
     *
     * 决定 LOD（Level of Detail）如何选择：
     *
     * VK_SAMPLER_MIPMAP_MODE_NEAREST：
     *   - 选择最接近的 mip level
     *   - 边界明显
     *
     * VK_SAMPLER_MIPMAP_MODE_LINEAR：
     *   - 在两个 mip level 之间插值
     *   - 更平滑（推荐）
     *
     * ============================
     * Mipmap LOD 计算模型（多视角统一表达）
     * ============================
     *
     * （1）工程直觉模型（基于比例缩放）
     *
     * \f[
     * \mathrm{LOD} = \log_{2}\left(\frac{S_{texture}}{S_{screen}}\right)
     * \f]
     *
     * （2）GPU真实计算模型（基于屏幕空间导数）
     *
     * \f[
     * \mathrm{LOD} = \log_{2}\left(\max\left(\|\nabla u\|, \|\nabla
     * v\|\right)\right)
     * \f]
     *
     * （3）Shader/硬件实现形式（ddx/ddy展开）
     *
     * \f[
     * \mathrm{LOD} =
     * \log_{2}\left(
     * \max\left(
     * \sqrt{\left(\frac{\partial u}{\partial x}\right)^2 + \left(\frac{\partial
     * v}{\partial x}\right)^2},
     * \sqrt{\left(\frac{\partial u}{\partial y}\right)^2 + \left(\frac{\partial
     * v}{\partial y}\right)^2}
     * \right)
     * \right)
     * \f]
     *
     * ============================
     * 三种模型关系说明
     * ============================
     *
     * - (1) 是“纹理尺度 vs 屏幕尺度”的工程近似（便于理解 mip 选择）
     * - (2) 是 GPU 在屏幕空间中的实际 LOD 决策模型
     * - (3) 是 Vulkan / GPU 在 shader derivative unit 中的底层实现
     *
     * ============================
     * 关键结论
     * ============================
     *
     * 三者本质等价，只是表达空间不同：
     *
     *   - (1) object space / intuition
     *   - (2) screen space gradient
     *   - (3) hardware derivatives (ddx/ddy)
     */
    VkSamplerMipmapMode mipmapMode { VK_SAMPLER_MIPMAP_MODE_LINEAR };

    /**
     * ============================
     * addressModeU / V / W（UV寻址模式）
     * ============================
     *
     * 当 UV 超出 [0,1] 区间时如何处理
     *
     * VK_SAMPLER_ADDRESS_MODE_REPEAT：
     *   - 平铺（tile）
     *
     * VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE：
     *   - 边缘拉伸（避免接缝）
     *
     * VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT：
     *   - 镜像平铺（减少重复感）
     *
     * W 维度：
     *   - 3D texture 或 array texture 使用
     */
    VkSamplerAddressMode addressModeU { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode addressModeV { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode addressModeW { VK_SAMPLER_ADDRESS_MODE_REPEAT };

    /**
     * ============================
     * maxAnisotropy（各向异性过滤）
     * ============================
     *
     * 用于改善“斜视角纹理模糊”的问题
     *
     * 原理：
     *   - 在高斜率方向增加采样数量
     *   - 避免远处地面/道路糊成一片
     *
     * 常见等级：
     *   1  -> 关闭
     *   4  -> 中等质量
     *   8  -> 常用
     *   16 -> 高质量（PC）
     *
     *  @note 前提条件：
     *   deviceFeatures.samplerAnisotropy == VK_TRUE
     *
     * @note 性能：
     *   - 显著增加纹理采样成本（尤其高频纹理）
     */
    float maxAnisotropy { 16.0F };

    /**
     * ============================
     * minLod / maxLod（LOD限制）
     * ============================
     *
     * 限制 mipmap 使用范围
     *
     * minLod：
     *   - 最小 mip level
     *   - 防止过度锐化或过度放大
     *
     * maxLod：
     *   - 最大 mip level
     *
     * VK_LOD_CLAMP_NONE：
     *   - 不限制（使用全部 mip levels）
     *
     * 用途：
     *   - UI贴图避免 mip blur
     *   - 渲染调试
     */
    float minLod { 0.0F };
    float maxLod { VK_LOD_CLAMP_NONE };
};

/**
 * ============================
 * Sampler（Vulkan资源封装）
 * ============================
 *
 * 生命周期管理 VkSampler：
 *   create → use in descriptor → destroy
 *
 * 特点：
 *  - 不依赖 image / imageView
 *  - 可跨纹理复用
 *  - 通常做成全局 cache（引擎级优化点）
 *
 * GPU内部：
 *  Sampler 会进入 descriptor cache，
 *  shader sampling 时由硬件直接执行 filtering。
 */
class Sampler {
public:
    Sampler() = default;

    /**
     * 禁止拷贝：
     * Vulkan 资源不能简单复制，否则 double free
     */
    Sampler(const Sampler &) = delete;
    Sampler &operator=(const Sampler &) = delete;

    /**
     * 移动语义：
     * 允许 ownership 转移（避免重复创建）
     */
    Sampler(Sampler &&other) noexcept;
    Sampler &operator=(Sampler &&other) noexcept;

    ~Sampler();

    /**
     * ============================
     * create
     * ============================
     *
     * 创建 VkSampler：
     *
     * 内部映射：
     * SamplerConfig → VkSamplerCreateInfo
     *
     * 关键 Vulkan 字段说明：
     *
     * - magFilter / minFilter → VkFilter
     * - mipmapMode → VkSamplerMipmapMode
     * - addressModeU/V/W → VkSamplerAddressMode
     * - anisotropyEnable → maxAnisotropy > 1.0
     * - maxAnisotropy → device feature dependent
     * - minLod / maxLod → LOD clamp
     *
     * @note 注意：
     * 必须确保：
     *   VkPhysicalDeviceFeatures::samplerAnisotropy == true
     */
    bool create(const Context &ctx, const SamplerConfig &config = {});

    /**
     * 返回 Vulkan 原生句柄
     * 用于 descriptor set 绑定
     */
    [[nodiscard]] VkSampler handle() const { return sampler_; }

    /**
     * 判断是否已创建成功
     */
    [[nodiscard]] bool is_valid() const { return sampler_ != VK_NULL_HANDLE; }

private:
    /**
     * 销毁 VkSampler
     *
     * 对应：
     *   vkDestroySampler(device, sampler, nullptr)
     */
    void destroy_();

private:
    /**
     * Vulkan 逻辑设备
     * Sampler 生命周期必须绑定 device
     */
    VkDevice device_ { VK_NULL_HANDLE };

    /**
     * Vulkan sampler handle
     */
    VkSampler sampler_ { VK_NULL_HANDLE };
};

} // namespace render
} // namespace lumen
