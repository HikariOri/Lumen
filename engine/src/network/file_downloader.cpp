#include "network/file_downloader.h"

#include <cpr/cpr.h>
#include <fstream>
#include <print>

namespace cake::network {

    std::optional<std::vector<uint8_t>>
    FileDownloader::DownloadToMemory(const std::string &url) {
        try {
            cpr::Response response = cpr::Get(cpr::Url { url });

            if (response.status_code == 200) {
                const std::string &text = response.text;
                std::vector<uint8_t> data(text.begin(), text.end());
                return data;
            }

            std::println(stderr, "Failed to download file. HTTP Status: {}",
                         response.status_code);
            return std::nullopt;
        } catch (const std::exception &e) {
            std::println(stderr, "Exception while downloading file: {}",
                         e.what());
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
                std::println(stderr, "Failed to open file for writing: {}",
                             filepath);
                return false;
            }

            file.write(reinterpret_cast<const char *>(data->data()),
                       data->size());
            file.close();
            return true;
        } catch (const std::exception &e) {
            std::println(stderr, "Exception while writing file: {}", e.what());
            return false;
        }
    }

    std::optional<std::string>
    FileDownloader::DownloadText(const std::string &url) {
        try {
            cpr::Response response = cpr::Get(cpr::Url { url });

            if (response.status_code == 200) {
                return response.text;
            } else {
                std::println(stderr, "Failed to download text. HTTP Status: {}",
                             response.status_code);
                return std::nullopt;
            }
        } catch (const std::exception &e) {
            std::println(stderr, "Exception while downloading text: {}",
                         e.what());
            return std::nullopt;
        }
    }

} // namespace cake::network
