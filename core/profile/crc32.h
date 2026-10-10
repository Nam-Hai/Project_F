#pragma once

#include <cstddef>
#include <cstdint>

namespace profile {

// CRC-32 (IEEE 802.3, the one used by zip and PNG: polynomial 0xEDB88320, reflected), computed a nibble at a time so
// that the table is only 16 entries. Check value: the CRC of the ASCII text "123456789" is 0xCBF43926.
class Crc32 {
public:
    void update(const uint8_t* data, std::size_t length) {
        static constexpr uint32_t TABLE[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                               0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                               0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
        for (std::size_t i = 0; i < length; ++i) {
            state_ ^= data[i];
            state_ = (state_ >> 4) ^ TABLE[state_ & 15u];
            state_ = (state_ >> 4) ^ TABLE[state_ & 15u];
        }
    }

    uint32_t value() const { return ~state_; }

private:
    uint32_t state_ = 0xFFFFFFFFu;
};

}  // namespace profile
