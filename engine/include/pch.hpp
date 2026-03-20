#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <flecs.h>

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

#include <glslang/Public/ShaderLang.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/spdlog.h>

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
