# 开发日志

## FileDownloader 文件下载器使用指南

### 概述

`FileDownloader` 是一个功能完善的HTTP/HTTPS文件下载器类，位于 `lumen::network` 命名空间中。它支持同步和异步下载，提供丰富的配置选项，包括超时设置、重试机制、自定义HTTP头等。

**位置**: `engine/include/network/file_downloader.h`

### 快速开始

#### 1. 基本用法（同步下载）

##### 下载文件到内存

```cpp
#include "network/file_downloader.h"

using namespace lumen::network;

// 简单版本 - 返回 std::optional<std::vector<uint8_t>>
auto data = FileDownloader::DownloadToMemory("https://example.com/image.jpg");
if (data.has_value()) {
    // 使用下载的数据
    std::vector<uint8_t> image_data = data.value();
    // 处理图片数据...
}

// 高级版本 - 返回 DownloadResponse（包含更详细信息）
DownloadOptions options;
options.SetTimeout(10000); // 设置10秒超时
auto response = FileDownloader::DownloadToMemory("https://example.com/image.jpg", options);
if (response.isSuccess()) {
    std::vector<uint8_t> image_data = response.data;
    std::cout << "下载耗时: " << response.elapsed_time << "秒" << std::endl;
    std::cout << "HTTP状态码: " << response.status_code << std::endl;
} else {
    std::cerr << "下载失败: " << response.error_message << std::endl;
}
```

##### 下载文件到本地

```cpp
// 简单版本
bool success = FileDownloader::DownloadToFile(
    "https://example.com/file.zip", 
    "downloaded_file.zip"
);
if (success) {
    std::cout << "文件下载成功！" << std::endl;
}

// 高级版本
DownloadOptions options;
options.SetTimeout(30000);
auto response = FileDownloader::DownloadToFile(
    "https://example.com/file.zip",
    "downloaded_file.zip",
    options
);
```

##### 下载文本内容

```cpp
// 简单版本
auto text = FileDownloader::DownloadText("https://example.com/api/data");
if (text.has_value()) {
    std::string content = text.value();
    // 处理文本内容...
}

// 高级版本
auto response = FileDownloader::DownloadText("https://example.com/api/data", options);
if (response.isSuccess()) {
    std::string content(response.data.begin(), response.data.end());
}
```

#### 2. 异步下载

##### 使用 std::future（推荐）

```cpp
#include <future>

// 启动异步下载，立即返回，不阻塞
auto future = FileDownloader::DownloadToMemoryAsync("https://example.com/large_file.bin");

// 继续执行其他任务...
std::cout << "下载已在后台进行..." << std::endl;
// ... 做其他事情 ...

// 需要结果时，调用 get() 等待并获取结果
auto response = future.get(); // 如果还没完成，会在这里等待
if (response.isSuccess()) {
    // 处理下载的数据
    auto data = response.data;
}

// 下载文件到本地（异步）
auto future2 = FileDownloader::DownloadToFileAsync(
    "https://example.com/file.zip",
    "downloaded.zip"
);
auto response2 = future2.get();

// 异步POST请求
auto future3 = FileDownloader::PostAsync(
    "https://api.example.com/endpoint",
    "post_data",
    options
);
auto response3 = future3.get();
```

##### 使用回调函数

```cpp
// 使用回调函数处理下载完成事件
FileDownloader::DownloadToMemoryAsync(
    "https://example.com/image.jpg",
    [](const DownloadResponse& response) {
        if (response.isSuccess()) {
            std::cout << "下载成功！数据大小: " << response.data.size() << " 字节" << std::endl;
            // 处理下载的数据
            auto data = response.data;
        } else {
            std::cerr << "下载失败: " << response.error_message << std::endl;
        }
    }
);

// 可以继续执行其他代码，回调函数会在下载完成后自动执行
std::cout << "下载任务已提交，继续执行其他代码..." << std::endl;
```

#### 3. 高级配置选项

##### DownloadOptions 配置

```cpp
DownloadOptions options;

// 设置超时时间（毫秒）
options.SetTimeout(10000); // 10秒

// 设置重试机制
options.SetRetry(3, 2000); // 最多重试3次，每次间隔2秒

// 添加自定义HTTP头
options.AddHeader("Authorization", "Bearer your_token_here");
options.AddHeader("X-Custom-Header", "custom_value");

// 设置User-Agent
options.user_agent = "MyApp/1.0";

// 禁用SSL证书验证（不推荐，仅用于测试）
options.verify_ssl = false;

// 使用配置选项下载
auto response = FileDownloader::DownloadToMemory("https://api.example.com/data", options);
```

##### 完整配置示例

```cpp
DownloadOptions options;
options.SetTimeout(15000);                    // 15秒超时
options.SetRetry(2, 1000);                    // 重试2次，间隔1秒
options.AddHeader("Authorization", "Bearer token123");
options.AddHeader("Accept", "application/json");
options.user_agent = "MyApplication/2.0";

auto response = FileDownloader::DownloadToMemory("https://api.example.com/data", options);
if (response.isSuccess()) {
    // 检查响应头
    auto contentType = response.headers.find("Content-Type");
    if (contentType != response.headers.end()) {
        std::cout << "内容类型: " << contentType->second << std::endl;
    }
}
```

#### 4. POST 请求

```cpp
// 普通POST请求
DownloadOptions options;
auto response = FileDownloader::Post(
    "https://api.example.com/endpoint",
    "key1=value1&key2=value2",
    options
);

// JSON POST请求（自动设置Content-Type头）
auto jsonResponse = FileDownloader::PostJson(
    "https://api.example.com/api/data",
    R"({"name": "test", "value": 123})"
);
```

#### 5. URL 可访问性检查

```cpp
// 检查URL是否可访问
bool accessible = FileDownloader::CheckUrl("https://example.com", 5000);
if (accessible) {
    std::cout << "URL可访问" << std::endl;
}
```

#### 6. 响应信息详解

```cpp
auto response = FileDownloader::DownloadToMemory("https://example.com/data", options);

// 检查是否成功
if (response.isSuccess()) {
    // 访问响应数据
    std::vector<uint8_t> data = response.data;
    
    // 获取HTTP状态码
    int status_code = response.status_code;  // 200, 201, 等
    
    // 获取响应头
    for (const auto& [key, value] : response.headers) {
        std::cout << key << ": " << value << std::endl;
    }
    
    // 获取请求耗时
    double elapsed = response.elapsed_time;  // 单位：秒
    std::cout << "请求耗时: " << elapsed << "秒" << std::endl;
} else {
    // 获取错误信息
    std::string error = response.error_message;
    std::cerr << "错误: " << error << std::endl;
}
```

### 使用场景示例

#### 场景1: 下载并保存图片

```cpp
bool DownloadImage(const std::string& url, const std::string& save_path) {
    DownloadOptions options;
    options.SetTimeout(10000);
    
    auto response = FileDownloader::DownloadToFile(url, save_path, options);
    return response.isSuccess();
}

// 使用
DownloadImage("https://example.com/image.jpg", "downloaded_image.jpg");
```

#### 场景2: 异步下载多个文件

```cpp
std::vector<std::future<DownloadResponse>> futures;

// 启动多个异步下载任务
futures.push_back(FileDownloader::DownloadToMemoryAsync("https://example.com/file1.jpg"));
futures.push_back(FileDownloader::DownloadToMemoryAsync("https://example.com/file2.jpg"));
futures.push_back(FileDownloader::DownloadToMemoryAsync("https://example.com/file3.jpg"));

// 等待所有下载完成
for (auto& future : futures) {
    auto response = future.get();
    if (response.isSuccess()) {
        std::cout << "下载成功，数据大小: " << response.data.size() << std::endl;
    }
}
```

#### 场景3: 带进度监控的下载（使用回调）

```cpp
DownloadOptions options;
options.progress_callback = [](int64_t downloaded, int64_t total) {
    if (total > 0) {
        double percent = (double)downloaded / total * 100.0;
        std::cout << "下载进度: " << percent << "% (" 
                  << downloaded << "/" << total << " 字节)" << std::endl;
    } else {
        std::cout << "已下载: " << downloaded << " 字节" << std::endl;
    }
};

auto response = FileDownloader::DownloadToMemory("https://example.com/large_file.zip", options);
```

#### 场景4: API调用（POST JSON）

```cpp
DownloadOptions options;
options.AddHeader("Authorization", "Bearer your_api_key");
options.SetTimeout(5000);

std::string json_data = R"({
    "query": "search term",
    "limit": 10
})";

auto response = FileDownloader::PostJson("https://api.example.com/search", json_data, options);
if (response.isSuccess()) {
    std::string json_response(response.data.begin(), response.data.end());
    // 解析JSON响应...
}
```

### API 参考

#### DownloadResponse 结构体

```cpp
struct DownloadResponse {
    int status_code;                              // HTTP状态码
    std::vector<uint8_t> data;                    // 响应数据（二进制）
    std::map<std::string, std::string> headers;   // HTTP响应头
    std::string error_message;                    // 错误信息（如果有）
    double elapsed_time;                          // 请求耗时（秒）
    
    bool isSuccess() const;  // 检查是否成功（状态码200-299）
};
```

#### DownloadOptions 结构体

```cpp
struct DownloadOptions {
    std::map<std::string, std::string> headers;           // 自定义HTTP头
    std::chrono::milliseconds timeout;                    // 超时时间（默认30秒）
    int max_retries;                                      // 最大重试次数（默认0）
    std::chrono::milliseconds retry_delay;                // 重试延迟（默认1秒）
    ProgressCallback progress_callback;                   // 进度回调函数
    bool verify_ssl;                                      // 是否验证SSL（默认true）
    std::string user_agent;                               // User-Agent（默认"FileDownloader/1.0"）
    
    void AddHeader(const std::string& key, const std::string& value);
    void SetTimeout(int milliseconds);
    void SetRetry(int max_retries, int delay_ms = 1000);
};
```

#### 主要方法

**同步方法:**
- `DownloadToMemory(url)` -> `std::optional<std::vector<uint8_t>>`
- `DownloadToMemory(url, options)` -> `DownloadResponse`
- `DownloadToFile(url, filepath)` -> `bool`
- `DownloadToFile(url, filepath, options)` -> `DownloadResponse`
- `DownloadText(url)` -> `std::optional<std::string>`
- `DownloadText(url, options)` -> `DownloadResponse`
- `Post(url, data, options)` -> `DownloadResponse`
- `PostJson(url, json_data, options)` -> `DownloadResponse`
- `CheckUrl(url, timeout_ms)` -> `bool`

**异步方法（返回future）:**
- `DownloadToMemoryAsync(url, options)` -> `std::future<DownloadResponse>`
- `DownloadToFileAsync(url, filepath, options)` -> `std::future<DownloadResponse>`
- `DownloadTextAsync(url, options)` -> `std::future<DownloadResponse>`
- `PostAsync(url, data, options)` -> `std::future<DownloadResponse>`

**异步方法（使用回调）:**
- `DownloadToMemoryAsync(url, callback, options)` -> `void`
- `DownloadToFileAsync(url, filepath, callback, options)` -> `void`

### 注意事项

1. **同步 vs 异步**: 
   - 同步方法会阻塞当前线程直到下载完成
   - 异步方法立即返回，不会阻塞，适合UI应用或需要并发下载的场景

2. **错误处理**:
   - 简单版本返回 `std::nullopt` 或 `bool` 表示失败
   - 高级版本返回 `DownloadResponse`，包含详细的错误信息

3. **内存管理**:
   - 下载到内存的方法会一次性加载整个文件到内存，不适合下载超大文件
   - 对于大文件，建议使用 `DownloadToFile` 直接保存到磁盘

4. **线程安全**:
   - `FileDownloader` 的所有方法都是线程安全的，可以在多线程环境中使用

5. **超时设置**:
   - 默认超时为30秒，可以通过 `SetTimeout()` 自定义
   - 超时后请求会失败，不会自动重试（除非设置了重试次数）

6. **重试机制**:
   - 默认不重试（`max_retries = 0`）
   - 只有服务器错误（状态码500+）才会触发重试
   - 客户端错误（4xx）不会重试

### 依赖项

- C++23 标准
- CPR 库（用于HTTP请求）
- `<future>`, `<thread>`, `<chrono>` 标准库

### 更新日志

- **初始版本**: 实现了基本的同步和异步下载功能
- 支持同步下载到内存/文件
- 支持异步下载（future和回调两种方式）
- 支持自定义HTTP头、超时、重试等配置选项
- 支持POST和JSON POST请求

