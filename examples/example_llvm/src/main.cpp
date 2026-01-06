#include <cstdint>
#include <cstdio>



int main() {

    uint16_t crc16 = 0x0A08;

    printf("CRC16 raw= 0x%04X", crc16);
    uint16_t saved_crc = crc16;
    printf("Before: crc16=0x%04X", crc16);
    printf("CRC16 高字节=0x%02X", (uint8_t)((crc16 >> 8) & 0xFF));
    printf("CRC16 低字节=0x%02X", (uint8_t)(crc16 & 0xFF));
    printf("After: crc16=0x%04x", crc16);
    printf("Saved:crc16=0x%04x", saved_crc);

    printf("Before:crc16=%d", crc16);
    printf("CRC16 高字节=%d", (uint8_t)((crc16 >> 8) & 0xFF));
    printf("CRC16 低字节=%d", (uint8_t)(crc16 & 0xFF));
    printf("After: crc16=%d", crc16);

    return 0;
}
