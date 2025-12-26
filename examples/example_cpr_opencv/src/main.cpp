#include <cstdio>
#include <opencv2/imgcodecs.hpp>
#include <print>
#include <vector>

#include <cpr/cpr.h>

#include <opencv2/opencv.hpp>

int main() {
    // 图片 URL
    std::string url = "https://www.loliapi.com/acg/pp/";

    // 使用 CPR 发送 GET 请求
    cpr::Response r = cpr::Get(cpr::Url { url });

    if (r.status_code == 200) {
        // 请求成功，获取图片数据
        std::vector<uchar> image_data(r.text.begin(), r.text.end());

        // 使用 OpenCV 解码图片
        cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);

        if (!img.empty()) {
            // 显示图片
            cv::imwrite("output.png", img);
            cv::imshow("Loaded Image", img);
            cv::waitKey(0); // 等待按键
        } else {
            std::println(stderr, "Error decoding the image!");
        }
    } else {
        std::println(stderr, "Failed to load image. HTTP Status: {}",
                     r.status_code);
    }

    return 0;
}
