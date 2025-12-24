#include <ZXing/BarcodeFormat.h>
#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>
#include <ZXing/TextUtfEncoding.h>
#include <opencv2/opencv.hpp>

int main() {
    std::string input = "Hello, ZXing! This is a PDF417 barcode.";

    ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::PDF417);
    auto bitMatrix = writer.encode(input, 200, 100);

    int width = bitMatrix.width();
    int height = bitMatrix.height();
    cv::Mat barcodeImage(height, width, CV_8UC1);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            barcodeImage.at<uint8_t>(y, x) = bitMatrix.get(x, y) ? 0 : 255;
        }
    }

    cv::namedWindow("PDF417 Barcode", cv::WINDOW_NORMAL);
    cv::imshow("PDF417 Barcode", barcodeImage);
    cv::waitKey(0);

    cv::imwrite("pdf417_barcode.png", barcodeImage);

    return 0;
}
