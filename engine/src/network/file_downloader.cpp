#include "network/file_downloader.h"
#include "core/logger.hpp"

#include <cpr/cpr.h>
#include <fstream>
#include <future>
#include <thread>

namespace lumen::network {

    // ========== ExecuteRequest 实现 ==========
    DownloadResponse
    FileDownloader::ExecuteRequest(const std::string &url,
                                   const std::string &method,
                                   const std::optional<std::string> &body,
                                   const DownloadOptions &options) {
        DownloadResponse result;
        auto start_time = std::chrono::steady_clock::now();

        // 构建CPR参数
        cpr::Url cpr_url { url };
        cpr::Timeout timeout { static_cast<int>(options.timeout.count()) };
        cpr::Header headers;
        cpr::VerifySsl verify_ssl { options.verify_ssl };

        // 添加默认User-Agent
        headers["User-Agent"] = options.userAgent;

        // 添加自定义HTTP头
        for (const auto &[key, value] : options.headers) {
            headers[key] = value;
        }

        // 执行请求（带重试）
        int retries_left = options.maxRetries;
        while (true) {
            try {
                cpr::Response response;

                if (method == "GET") {
                    response = cpr::Get(cpr_url, headers, timeout, verify_ssl);
                } else if (method == "POST") {
                    cpr::Body body_data { body.value_or("") };
                    response = cpr::Post(cpr_url, headers, body_data, timeout,
                                         verify_ssl);
                } else if (method == "PUT") {
                    cpr::Body body_data { body.value_or("") };
                    response = cpr::Put(cpr_url, headers, body_data, timeout,
                                        verify_ssl);
                } else if (method == "DELETE") {
                    response =
                        cpr::Delete(cpr_url, headers, timeout, verify_ssl);
                } else {
                    result.errorMessage = "Unsupported HTTP method: " + method;
                    return result;
                }

                auto end_time = std::chrono::steady_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time);

                result.statusCode = response.status_code;
                result.elapsedTime = duration.count() / 1000.0;

                // 复制响应头
                for (const auto &[key, value] : response.header) {
                    result.headers[key] = value;
                }

                // 复制响应数据
                const std::string &text = response.text;
                result.data.assign(text.begin(), text.end());

                // 如果成功，返回结果
                if (result.isSuccess()) {
                    return result;
                }

                // 如果失败且还有重试机会
                if (retries_left > 0 && response.status_code >= 500) {
                    retries_left--;
                    result.errorMessage = "Server error " +
                                          std::to_string(response.status_code) +
                                          ", retrying...";
                    std::this_thread::sleep_for(options.retryDelay);
                    continue;
                }

                // 失败且不重试
                result.errorMessage =
                    "HTTP " + std::to_string(response.status_code);
                return result;

            } catch (const std::exception &e) {
                auto end_time = std::chrono::steady_clock::now();
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time);
                result.elapsedTime = duration.count() / 1000.0;

                if (retries_left > 0) {
                    retries_left--;
                    result.errorMessage =
                        std::string("Exception: ") + e.what() + ", retrying...";
                    std::this_thread::sleep_for(options.retryDelay);
                    continue;
                }

                result.errorMessage = e.what();
                return result;
            }
        }
    }

    // ========== 同步下载方法实现 ==========

    std::optional<std::vector<uint8_t>>
    FileDownloader::DownloadToMemory(const std::string &url) {
        try {
            cpr::Response response = cpr::Get(cpr::Url { url });

            if (response.status_code == 200) {
                const std::string &text = response.text;
                std::vector<uint8_t> data(text.begin(), text.end());
                return data;
            }

            LUMEN_LOG_ERROR("Failed to download file. HTTP Status: {}",
                            static_cast<int>(response.status_code));
            return std::nullopt;
        } catch (const std::exception &e) {
            LUMEN_LOG_ERROR("Exception while downloading file: {}", e.what());
            return std::nullopt;
        }
    }

    bool FileDownloader::DownloadToFile(const std::string &url,
                                        const std::string &filepath) {
        auto data = DownloadToMemory(url);

        if (!data.has_value()) {
            return false;
        }

        try {
            std::ofstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                LUMEN_LOG_ERROR("Failed to open file for writing: {}",
                                filepath);
                return false;
            }

            file.write(reinterpret_cast<const char *>(data->data()),
                       data->size());
            file.close();
            return true;
        } catch (const std::exception &e) {
            LUMEN_LOG_ERROR("Exception while writing file: {}", e.what());
            return false;
        }
    }

    // 高级版本（带options）
    DownloadResponse
    FileDownloader::DownloadToMemory(const std::string &url,
                                     const DownloadOptions &options) {
        return ExecuteRequest(url, "GET", std::nullopt, options);
    }

    DownloadResponse
    FileDownloader::DownloadToFile(const std::string &url,
                                   const std::string &filepath,
                                   const DownloadOptions &options) {
        auto response = DownloadToMemory(url, options);

        if (!response.isSuccess()) {
            return response;
        }

        try {
            std::ofstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                response.errorMessage =
                    "Failed to open file for writing: " + filepath;
                return response;
            }

            file.write(reinterpret_cast<const char *>(response.data.data()),
                       static_cast<std::streamsize>(response.data.size()));
            file.close();
            return response;
        } catch (const std::exception &e) {
            response.errorMessage =
                std::string("Exception while writing file: ") + e.what();
            return response;
        }
    }

    std::optional<std::string>
    FileDownloader::DownloadText(const std::string &url) {
        try {
            cpr::Response response = cpr::Get(cpr::Url { url });

            if (response.status_code == 200) {
                return response.text;
            }
            LUMEN_LOG_ERROR("Failed to download text. HTTP Status: {}",
                            static_cast<int>(response.status_code));
            return std::nullopt;
        } catch (const std::exception &e) {
            LUMEN_LOG_ERROR("Exception while downloading text: {}", e.what());
            return std::nullopt;
        }
    }

    // 高级版本（带options）
    DownloadResponse
    FileDownloader::DownloadText(const std::string &url,
                                 const DownloadOptions &options) {
        return DownloadToMemory(url, options);
    }

    DownloadResponse FileDownloader::Post(const std::string &url,
                                          const std::string &data,
                                          const DownloadOptions &options) {
        return ExecuteRequest(url, "POST", data, options);
    }

    DownloadResponse FileDownloader::PostJson(const std::string &url,
                                              const std::string &json_data,
                                              const DownloadOptions &options) {
        DownloadOptions json_options = options;
        json_options.addHeader("Content-Type", "application/json");
        return Post(url, json_data, json_options);
    }

    bool FileDownloader::CheckUrl(const std::string &url, int timeout_ms) {
        DownloadOptions options;
        options.setTimeout(timeout_ms);
        auto response = DownloadToMemory(url, options);
        return response.statusCode > 0 &&
               (response.statusCode < 400 || response.statusCode == 404);
    }

    // ========== 异步下载方法实现 ==========

    std::future<DownloadResponse>
    FileDownloader::DownloadToMemoryAsync(const std::string &url,
                                          const DownloadOptions &options) {
        return std::async(std::launch::async, [url, options]() {
            return DownloadToMemory(url, options);
        });
    }

    std::future<DownloadResponse>
    FileDownloader::DownloadToFileAsync(const std::string &url,
                                        const std::string &filepath,
                                        const DownloadOptions &options) {
        return std::async(std::launch::async, [url, filepath, options]() {
            return DownloadToFile(url, filepath, options);
        });
    }

    std::future<DownloadResponse>
    FileDownloader::DownloadTextAsync(const std::string &url,
                                      const DownloadOptions &options) {
        return std::async(std::launch::async, [url, options]() {
            return DownloadText(url, options);
        });
    }

    std::future<DownloadResponse>
    FileDownloader::PostAsync(const std::string &url, const std::string &data,
                              const DownloadOptions &options) {
        return std::async(std::launch::async, [url, data, options]() {
            return Post(url, data, options);
        });
    }

    void
    FileDownloader::DownloadToMemoryAsync(const std::string &url,
                                          const CompletionCallback &callback,
                                          const DownloadOptions &options) {
        std::thread([url, callback, options]() {
            auto response = DownloadToMemory(url, options);
            callback(response);
        }).detach();
    }

    void FileDownloader::DownloadToFileAsync(const std::string &url,
                                             const std::string &filepath,
                                             const CompletionCallback &callback,
                                             const DownloadOptions &options) {
        std::thread([url, filepath, callback, options]() {
            auto response = DownloadToFile(url, filepath, options);
            callback(response);
        }).detach();
    }

} // namespace lumen::network
