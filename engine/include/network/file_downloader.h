#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cake {
    namespace network {

        /**
         * @brief HTTP响应信息结构体
         */
        struct DownloadResponse {
            int status_code = 0;                        // HTTP状态码
            std::vector<uint8_t> data;                  // 响应数据
            std::map<std::string, std::string> headers; // 响应头
            std::string error_message;                  // 错误信息
            double elapsed_time = 0.0;                  // 请求耗时（秒）

            bool isSuccess() const {
                return status_code >= 200 && status_code < 300;
            }
        };

        /**
         * @brief 下载进度回调函数类型
         * @param downloaded 已下载的字节数
         * @param total 总字节数（如果未知则为-1）
         */
        using ProgressCallback =
            std::function<void(int64_t downloaded, int64_t total)>;

        /**
         * @brief 下载配置选项
         */
        struct DownloadOptions {
            std::map<std::string, std::string> headers;  // 自定义HTTP头
            std::chrono::milliseconds timeout { 30000 }; // 超时时间（默认30秒）
            int max_retries = 0; // 最大重试次数（默认不重试）
            std::chrono::milliseconds retry_delay {
                1000
            }; // 重试延迟（默认1秒）
            ProgressCallback progress_callback;            // 进度回调函数
            bool verify_ssl = true;                        // 是否验证SSL证书
            std::string user_agent = "FileDownloader/1.0"; // User-Agent

            // 添加自定义HTTP头
            void AddHeader(const std::string &key, const std::string &value) {
                headers[key] = value;
            }

            // 设置超时时间
            void SetTimeout(int milliseconds) {
                timeout = std::chrono::milliseconds(milliseconds);
            }

            // 设置重试参数
            void SetRetry(int max_retries, int delay_ms = 1000) {
                this->max_retries = max_retries;
                this->retry_delay = std::chrono::milliseconds(delay_ms);
            }
        };

        /**
         * @brief 文件下载器类，用于从 HTTP/HTTPS URL 下载文件
         */
        class FileDownloader {
        public:
            /**
             * @brief 下载文件到内存（简单版本）
             * @param url 要下载的文件URL
             * @return 下载的文件数据，失败时返回std::nullopt
             */
            static std::optional<std::vector<uint8_t>>
            DownloadToMemory(const std::string &url);

            /**
             * @brief 下载文件到内存（高级版本，支持配置选项）
             * @param url 要下载的文件URL
             * @param options 下载配置选项
             * @return 下载响应信息
             */
            static DownloadResponse
            DownloadToMemory(const std::string &url,
                             const DownloadOptions &options);

            /**
             * @brief 下载文件并保存到本地（简单版本）
             * @param url 要下载的文件URL
             * @param filepath 保存文件的本地路径
             * @return 是否下载成功
             */
            static bool DownloadToFile(const std::string &url,
                                       const std::string &filepath);

            /**
             * @brief 下载文件并保存到本地（高级版本，支持配置选项）
             * @param url 要下载的文件URL
             * @param filepath 保存文件的本地路径
             * @param options 下载配置选项
             * @return 下载响应信息
             */
            static DownloadResponse
            DownloadToFile(const std::string &url, const std::string &filepath,
                           const DownloadOptions &options);

            /**
             * @brief 获取HTTP响应的文本内容（简单版本）
             * @param url 要请求的URL
             * @return 响应文本内容，失败时返回std::nullopt
             */
            static std::optional<std::string>
            DownloadText(const std::string &url);

            /**
             * @brief 获取HTTP响应的文本内容（高级版本）
             * @param url 要请求的URL
             * @param options 下载配置选项
             * @return 下载响应信息
             */
            static DownloadResponse
            DownloadText(const std::string &url,
                         const DownloadOptions &options);

            /**
             * @brief 发送POST请求
             * @param url 请求的URL
             * @param data POST数据
             * @param options 请求配置选项
             * @return 下载响应信息
             */
            static DownloadResponse Post(const std::string &url,
                                         const std::string &data,
                                         const DownloadOptions &options = {});

            /**
             * @brief 发送POST请求（JSON格式）
             * @param url 请求的URL
             * @param json_data JSON字符串
             * @param options 请求配置选项
             * @return 下载响应信息
             */
            static DownloadResponse
            PostJson(const std::string &url, const std::string &json_data,
                     const DownloadOptions &options = {});

            /**
             * @brief 检查URL是否可访问
             * @param url 要检查的URL
             * @param timeout_ms 超时时间（毫秒）
             * @return 是否可访问
             */
            static bool CheckUrl(const std::string &url, int timeout_ms = 5000);

        private:
            /**
             * @brief 执行HTTP请求的核心函数
             */
            static DownloadResponse
            ExecuteRequest(const std::string &url, const std::string &method,
                           const std::optional<std::string> &body,
                           const DownloadOptions &options);
        };

    } // namespace network
} // namespace cake
