#include <print>

int main() {

    uint16_t crc16 = 0x0A08;

    std::println("CRC16 raw = 0x{:#04X}", crc16);

    uint16_t saved_crc = crc16;

    std::println("Before: crc16 = 0x{:#04X}", crc16);
    std::println("CRC16 high byte = 0x{:#02X}",
                 static_cast<uint8_t>((crc16 >> 8) & 0xFF));
    std::println("CRC16 low byte  = 0x{:#02X}",
                 static_cast<uint8_t>(crc16 & 0xFF));

    std::println("After: crc16  = 0x{:#04x}", crc16);
    std::println("Saved: crc16 = 0x{:#04x}", saved_crc);

    std::println("Before: crc16 = {}", crc16);
    std::println("CRC16 high byte = {}",
                 static_cast<uint8_t>((crc16 >> 8) & 0xFF));
    std::println("CRC16 low byte  = {}", static_cast<uint8_t>(crc16 & 0xFF));
    std::println("After: crc16  = {}", crc16);

    return 0;
}
