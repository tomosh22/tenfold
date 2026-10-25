#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tf::gfx {

// Minimal PNG support with no dependencies. The encoder emits 8-bit RGBA, filter 0, zlib *stored*
// (uncompressed) deflate blocks: files are large but trivially valid and byte-for-byte deterministic.
std::vector<uint8_t> encodePngRgba(const uint8_t* rgba, int w, int h);
bool writePngFile(const std::string& path, const uint8_t* rgba, int w, int h);

// Decoder for exactly what encodePngRgba produces (RGBA8, filter 0, stored blocks); verifies CRCs and
// Adler-32. Used by the golden-image tests; it is not a general PNG reader.
bool decodePngRgba(const std::vector<uint8_t>& png, int& w, int& h, std::vector<uint8_t>& rgba);
bool readPngFile(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba);

uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0);
uint32_t adler32(const uint8_t* d, size_t n);

}  // namespace tf::gfx
