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
                                  const char* title) {
    if (!ctx.has_device()) {
        return;
    }
    ImGui::Begin(title);
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

} // namespace ui
} // namespace lumen
