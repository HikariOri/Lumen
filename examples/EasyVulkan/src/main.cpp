#include "GlfwGeneral.hpp"
#include "VKBase.h"

int main() {
    if (!InitializeWindow({ 1280, 720 })) {
        return -1;
    }

    vulkan::fence fence { VK_FENCE_CREATE_SIGNALED_BIT }; // 以置位状态创建栅栏
    vulkan::semaphore semaphore_imageIsAvailable;
    vulkan::semaphore semaphore_renderingIsOver;

    while (!glfwWindowShouldClose(pWindow)) {

        /*假定这里有同步执行的A部分代码*/

        // 等待并重置 fence
        fence.WaitAndReset();

        /*假定这里有异步执行的B部分代码*/

        glfwPollEvents();
        TitleFps();
    }

    TerminateWindow();

    return 0;
}
