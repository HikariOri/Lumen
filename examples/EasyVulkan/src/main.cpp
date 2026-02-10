#include "GlfwGeneral.hpp"

int main() {
    if (!InitializeWindow({ 1280, 720 })) {
        return -1;
    }

    while (!glfwWindowShouldClose(pWindow)) {

        /*渲染过程，待填充*/

        glfwPollEvents();
        TitleFps();
    }

    TerminateWindow();

    return 0;
}
