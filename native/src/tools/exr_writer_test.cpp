#include "prism/render/exr_writer.hpp"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

template <typename T>
T readPod(const std::vector<uint8_t> &bytes, size_t offset) {
    T out{};
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return out;
}

bool nearHalf(float actual, float expected, float relTol = 0.005f) {
    return std::fabs(actual - expected) <= std::max(1e-3f, std::fabs(expected) * relTol);
}

} // namespace

int main() {
    const uint32_t width = 3;
    const uint32_t height = 2;
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    const float infVal = std::numeric_limits<float>::infinity();

    // 3x2 RGBA linear HDR test image: includes highlights > 1.0, negative clamp, NaN and Inf sanitization.
    const std::vector<float> pixels = {
        0.18f, 0.5f, 1.25f, 1.0f,     12.5f, 250.0f, 0.02f, 1.0f,    -4.0f, nanVal, infVal, 1.0f,
        3.0f,  6.0f, 9.0f,  1.0f,     0.0f,  1000.0f, 42.0f, 1.0f,   80000.0f, 0.25f, 0.75f, 1.0f,
    };

    const std::vector<uint8_t> exr = exr_writer::encodeHalfRgbExr(width, height, pixels.data());
    if (exr.size() < 128) {
        std::cerr << "encoded EXR too small: " << exr.size() << "\n";
        return 1;
    }

    // Check OpenEXR magic and version.
    if (readPod<int32_t>(exr, 0) != 20000630 || readPod<int32_t>(exr, 4) != 2) {
        std::cerr << "invalid OpenEXR header magic or version\n";
        return 1;
    }

    // Walk header attributes until the terminating 0 byte.
    size_t cursor = 8;
    int attributeCount = 0;
    while (cursor < exr.size() && exr[cursor] != 0) {
        while (cursor < exr.size() && exr[cursor] != 0) cursor++;
        cursor++; // past attribute name '\0'
        while (cursor < exr.size() && exr[cursor] != 0) cursor++;
        cursor++; // past attribute type '\0'
        const int32_t attrSize = readPod<int32_t>(exr, cursor);
        cursor += sizeof(int32_t) + static_cast<size_t>(attrSize);
        attributeCount++;
    }
    if (attributeCount != 8 || cursor >= exr.size() || exr[cursor] != 0) {
        std::cerr << "unexpected header layout (attributes=" << attributeCount << ")\n";
        return 1;
    }
    cursor++; // past end-of-header '\0'

    const uint64_t row0Offset = readPod<uint64_t>(exr, cursor);
    const uint64_t row1Offset = readPod<uint64_t>(exr, cursor + sizeof(uint64_t));
    if (row0Offset != cursor + 2 * sizeof(uint64_t)) {
        std::cerr << "row 0 offset mismatch\n";
        return 1;
    }

    // Verify row 0: y=0, byteSize = 3 * 3 * 2 = 18, planar B, G, R.
    if (readPod<int32_t>(exr, row0Offset) != 0 || readPod<int32_t>(exr, row0Offset + 4) != 18) {
        std::cerr << "row 0 block header mismatch\n";
        return 1;
    }
    const size_t row0Data = static_cast<size_t>(row0Offset) + 8;
    const auto halfAt = [&](size_t dataBase, size_t plane, size_t x) {
        return glm::unpackHalf1x16(readPod<uint16_t>(exr, dataBase + (plane * width + x) * sizeof(uint16_t)));
    };

    // Pixel (0,0) = RGB(0.18, 0.5, 1.25) -> B=plane0, G=plane1, R=plane2.
    if (!nearHalf(halfAt(row0Data, 2, 0), 0.18f) || !nearHalf(halfAt(row0Data, 1, 0), 0.5f) ||
        !nearHalf(halfAt(row0Data, 0, 0), 1.25f)) {
        std::cerr << "pixel (0,0) round-trip mismatch\n";
        return 1;
    }
    // Pixel (1,0) = HDR RGB(12.5, 250.0, 0.02).
    if (!nearHalf(halfAt(row0Data, 2, 1), 12.5f) || !nearHalf(halfAt(row0Data, 1, 1), 250.0f) ||
        !nearHalf(halfAt(row0Data, 0, 1), 0.02f)) {
        std::cerr << "pixel (1,0) HDR round-trip mismatch\n";
        return 1;
    }
    // Pixel (2,0) = (-4.0, NaN, Inf) -> all sanitized to 0.0f.
    if (halfAt(row0Data, 2, 2) != 0.0f || halfAt(row0Data, 1, 2) != 0.0f || halfAt(row0Data, 0, 2) != 0.0f) {
        std::cerr << "pixel (2,0) sanitization failed\n";
        return 1;
    }

    // Verify row 1 pixel (2,1) clamps 80000.0f to max finite half 65504.0f.
    const size_t row1Data = static_cast<size_t>(row1Offset) + 8;
    if (halfAt(row1Data, 2, 2) != 65504.0f) {
        std::cerr << "pixel (2,1) half max clamp failed: " << halfAt(row1Data, 2, 2) << "\n";
        return 1;
    }

    // Verify Unicode directory and file names (Spanish, CJK and Cyrillic) for both PNG and EXR atomic writers.
    std::error_code ec;
    const std::filesystem::path unicodeRoot =
        std::filesystem::temp_directory_path(ec) / std::u16string(u"radiante_foto_niño_雪_фото_test");
    std::filesystem::remove_all(unicodeRoot, ec);
    const std::filesystem::path unicodePng = unicodeRoot / std::u16string(u"captura_niño_雪_фото.png");
    const std::filesystem::path unicodeExr = unicodeRoot / std::u16string(u"captura_niño_雪_фото.exr");

    const std::vector<uint8_t> rgba8(width * height * 4u, 200u);
    const std::string pngErr = exr_writer::writeRgbaPngFile(unicodePng, width, height, rgba8.data());
    const std::string exrErr = exr_writer::writeHalfRgbExrFile(unicodeExr, width, height, pixels.data(), true);
    const bool pngExists = std::filesystem::exists(unicodePng, ec) && std::filesystem::file_size(unicodePng, ec) > 32;
    const bool exrExists = std::filesystem::exists(unicodeExr, ec) && std::filesystem::file_size(unicodeExr, ec) > 128;
    std::filesystem::remove_all(unicodeRoot, ec);
    if (!pngErr.empty() || !exrErr.empty() || !pngExists || !exrExists) {
        std::cerr << "Unicode PNG/EXR write failed: png='" << pngErr << "' exr='" << exrErr << "'\n";
        return 1;
    }

    return 0;
}
