#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace exr_writer {

// Encodes an image of linear scene-referred RGBA float pixels (4 floats per pixel, row-major from top-left) as a
// standard OpenEXR 2.0 single-part uncompressed HALF (16-bit IEEE-754 float) BGR scanline stream in memory.
std::vector<uint8_t> encodeHalfRgbExr(uint32_t width, uint32_t height, const float *rgbaLinear, bool flipY = false);

// Writes `rgbaLinear` atomically to `path` (via a `.tmp` sibling renamed into place). Returns empty string on success,
// or a human-readable error message on failure.
std::string writeHalfRgbExrFile(const std::filesystem::path &path,
                                uint32_t width,
                                uint32_t height,
                                const float *rgbaLinear,
                                bool flipY = false);

// Writes 8-bit RGBA pixels atomically to `path` as a PNG via `std::ofstream` (full Unicode `std::filesystem::path`
// support on Windows and Linux without narrow ANSI path conversion). Returns empty string on success, or an error.
std::string writeRgbaPngFile(const std::filesystem::path &path, uint32_t width, uint32_t height, const uint8_t *rgba);

} // namespace exr_writer
