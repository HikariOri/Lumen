# ============================================================================
# 物理引擎和ECS
# ============================================================================
find_package(Bullet CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)

# ============================================================================
# 图形和窗口库
# ============================================================================
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# ============================================================================
# 着色器相关
# ============================================================================
find_package(glslang CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

# ============================================================================
# 3D模型加载
# ============================================================================
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_package(tinyobjloader CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_package(fastgltf CONFIG REQUIRED)

# ============================================================================
# UI库
# ============================================================================
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)

# ============================================================================
# 日志库
# ============================================================================
find_package(quill CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# ============================================================================
# 媒体库 (SDL)
# ============================================================================
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)

# ============================================================================
# Vulkan相关
# ============================================================================
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)

# ============================================================================
# 工具库
# ============================================================================
find_package(Stb REQUIRED)
find_package(tabulate CONFIG REQUIRED)
find_package(indicators CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(SqliteOrm CONFIG REQUIRED)

find_package(ZXing CONFIG REQUIRED)

find_package(OpenCV CONFIG REQUIRED)

find_package(cpr CONFIG REQUIRED)

find_package(doctest CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)

find_package(nlohmann_json CONFIG REQUIRED)

find_package(simdjson CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS math)
find_package(Eigen3 CONFIG REQUIRED)
find_package(ghc_filesystem CONFIG REQUIRED)
find_package(eventpp CONFIG REQUIRED)
find_package(uvw CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
