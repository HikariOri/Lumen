/**
 * @file gpu_capabilities_panel.cpp
 * @brief GPU Capabilities 面板实现
 */

#include "ui/gpu_capabilities_panel.hpp"
#include "render/context.hpp"

#include <imgui.h>
#include <vulkan/vulkan.h>

namespace lumen {
namespace ui {

void imgui_gpu_capabilities_panel(const render::Context& ctx,
                                  const char* title, bool* p_open) {
    if (!ctx.has_device()) {
        return;
    }
    bool began = false;
    if (p_open) {
        began = ImGui::Begin(title, p_open);
    } else {
        began = ImGui::Begin(title);
    }
    if (!began) {
        ImGui::End();
        return;
    }
    const auto gpuInfo = ctx.physical_device_info();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s",
                       gpuInfo.deviceName.c_str());
    ImGui::Text("Type: %s",
                render::device_type_name(gpuInfo.deviceType));
    ImGui::Text("Vendor ID: 0x%04X", gpuInfo.vendorId);
    ImGui::Text("Device ID: 0x%04X", gpuInfo.deviceId);
    ImGui::Text("API: %u.%u.%u", VK_VERSION_MAJOR(gpuInfo.apiVersion),
                VK_VERSION_MINOR(gpuInfo.apiVersion),
                VK_VERSION_PATCH(gpuInfo.apiVersion));
    ImGui::Text("Driver: 0x%08X", gpuInfo.driverVersion);
    if (gpuInfo.deviceLocalMemoryBytes > 0) {
        const double vramMB = static_cast<double>(gpuInfo.deviceLocalMemoryBytes) /
                             (1024.0 * 1024.0);
        ImGui::Text("VRAM: %.0f MiB", vramMB);
    }
    ImGui::Separator();
    const auto& limits = ctx.physical_device_properties().limits;
    ImGui::Text("maxImageDimension2D: %u", limits.maxImageDimension2D);
    ImGui::Text("maxUniformBufferRange: %u KiB",
                limits.maxUniformBufferRange / 1024);
    ImGui::Text("maxStorageBufferRange: %u KiB",
                limits.maxStorageBufferRange / 1024);
    ImGui::Text("maxPushConstantsSize: %u bytes",
                limits.maxPushConstantsSize);
    ImGui::End();
}

GpuCapabilitiesPanel::GpuCapabilitiesPanel(const render::Context& ctx)
    : ctx_(&ctx) {}

void GpuCapabilitiesPanel::on_imgui_render() {
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f),
                             ImGuiCond_FirstUseEver);
    imgui_gpu_capabilities_panel(*ctx_, "GPU Capabilities", nullptr);
}

} // namespace ui
} // namespace lumen
