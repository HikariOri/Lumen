/**
 * @file    pipeline.h
 * @brief   Pipeline クラスの定義
 *
 * Pipeline クラスは Vulkan パイプラインを扱うためのクラスです。
 * 詳細な説明や制約条件、使い方の例などを書くと親切です。
 */

#pragma once

#include <vulkan/vulkan.hpp>

namespace lumen {

    /**
     * # Pipeline 类
     *
     * Pipeline 类用于管理 ** 资源 X **。主要功能包括：
     *
     * - 初始化资源
     * - 执行操作
     * - 清理资源
     *
     * ## 示例用法
     *
     * ```cpp
     * Pipeline foo;
     * foo.doSomething(42);
     * std::cout << foo.getValue() << std::endl;
     * ```
     */
    class Pipeline {
    public:
        vk::Pipeline pipeline;

        /**
         * @brief 示例函数 — 展示如何使用 Foo 类
         *
         * <table>
         * <caption>配置选项说明</caption>
         * <tr><th>选项</th><th>说明</th><th>默认值</th>
         * <tr><td>enableFeature</td><td>是否启用特性 X</td><td>false</td>
         * <tr><td>maxCount</td><td>最大次数限制</td><td>100</td>
         * </table>
         *
         * 使用示例:
         * @code{.cpp}
         * Foo foo;
         * if (!foo.exampleUsage(42, "data")) {
         *     std::cerr << "Error\n";
         * }
         * int result = foo.getValue();
         * std::cout << "Result: " << result << std::endl;
         * @endcode
         *
         * @param x  操作所需的整数参数 (例: 42)
         * @param s  操作所需的字符串参数 (例: "data")
         * @return `true` 表示操作成功, `false` 表示失败
         *
         * @details
         * 如果 x 为负数或 s 为空串, 则函数返回 false (表示错误).
         * 否则执行某些操作, 并更新内部状态, 后续可以通过 getValue() 获取结果.
         */
        bool exampleUsage(int x, const std::string &s);
    };

    /*! Invisible class because of truncation */
    class Invisible {};

    /*! Truncated class, inheritance relation is hidden */
    class Truncated : public Invisible {};

    /* Class not documented with doxygen comments */
    class Undocumented {};

    /*! Class that is inherited using public inheritance */
    class PublicBase : public Truncated {};

    /*! A template class */
    template <class T>
    class Templ {};

    /*! Class that is inherited using protected inheritance */
    class ProtectedBase {};

    /*! Class that is inherited using private inheritance */
    class PrivateBase {};

    /*! Class that is used by the Inherited class */
    class Used {};

    /*! Super class that inherits a number of other classes */
    class Inherited : public PublicBase,
                      protected ProtectedBase,
                      private PrivateBase,
                      public Undocumented,
                      public Templ<int> {
    private:
        Used *m_usedClass;
    };

} // namespace lumen
