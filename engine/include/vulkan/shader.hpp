#include <print>

#include <vulkan/vulkan.h>

namespace lumen::vulkan {

    class ShaderModule {
        VkShaderModule handle = VK_NULL_HANDLE;

    public:
        ShaderModule() = default;

        ShaderModule(VkShaderModuleCreateInfo &createInfo) {
            create(createInfo);
        }

        ShaderModule(const char *filepath /*VkShaderModuleCreateFlags flags*/) {
            create(filepath);
        }

        ShaderModule(
            size_t codeSize,
            const uint32_t *pCode /*VkShaderModuleCreateFlags flags*/) {
            create(codeSize, pCode);
        }

        ShaderModule(ShaderModule &&other) noexcept { MoveHandle; }

        ~ShaderModule() { 
        vkDestroyShaderModule(handle) }

        // Getter
        DefineHandleTypeOperator;
        DefineAddressFunction;
        // Const Function

        VkPipelineShaderStageCreateInfo
        StageCreateInfo(VkShaderStageFlagBits stage,
                        const char *entry = "main") const {
            return {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // sType
                nullptr,                                             // pNext
                0,                                                   // flags
                stage,                                               // stage
                handle,                                              // module
                entry,                                               // pName
                nullptr // pSpecializationInfo
            };
        }

        // Non-const Function
        VkResult create(VkShaderModuleCreateInfo &createInfo) {
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            VkResult result = vkCreateShaderModule(
                graphicsBase::Base().Device(), &createInfo, nullptr, &handle);
            if (result) {
                std::println("[ shader ] ERROR\nFailed to create a shader "
                             "module!\nError code: {}\n",
                             string_VkResult(result));
            }
            return result;
        }

        VkResult
        create(const char *filepath /*VkShaderModuleCreateFlags flags*/) {
            std::ifstream file(filepath, std::ios::ate | std::ios::binary);
            if (!file) {
                std::println("[ shader ] ERROR\nFailed to open the file: {}\n",
                             filepath);
                return VK_RESULT_MAX_ENUM; // 没有合适的错误代码，别用VK_ERROR_UNKNOWN
            }
            size_t fileSize = size_t(file.tellg());
            std::vector<uint32_t> binaries(fileSize / 4);
            file.seekg(0);
            file.read(reinterpret_cast<char *>(binaries.data()), fileSize);
            file.close();
            return create(fileSize, binaries.data());
        }

        VkResult
        create(size_t codeSize,
               const uint32_t *pCode /*VkShaderModuleCreateFlags flags*/) {
            VkShaderModuleCreateInfo createInfo = { .codeSize = codeSize,
                                                    .pCode = pCode };
            return create(createInfo);
        }
    };

} // namespace lumen::vulkan
