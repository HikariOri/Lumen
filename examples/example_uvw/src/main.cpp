#include <iostream>
#include <uvw.hpp>

int main() {
    // 获取默认事件循环
    auto loop = uvw::loop::get_default();

    // 创建一个定时器资源
    auto timer = loop->resource<uvw::timer_handle>();

    // 注册事件监听（在定时器触发时调用）
    timer->on<uvw::timer_event>(
        [](const uvw::timer_event &, uvw::timer_handle &) {
            std::cout << "Timer fired!" << '\n';
        });

    // 启动定时器：延迟 1000ms（1s）执行，不重复
    timer->start(std::chrono::milliseconds(1000), std::chrono::milliseconds(0));

    // 运行事件循环
    loop->run();

    return 0;
}
