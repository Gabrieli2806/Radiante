#include "prism/render/exr_writer.hpp"

#include <glm/gtc/packing.hpp>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace exr_writer {
namespace {

void appendBytes(std::vector<uint8_t> &out, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void appendCString(std::vector<uint8_t> &out, const char *text) {
    appendBytes(out, text, std::strlen(text) + 1);
}

template <typename T>
void appendPod(std::vector<uint8_t> &out, const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    appendBytes(out, &value, sizeof(T));
}

void appendAttributeHeader(std::vector<uint8_t> &out, const char *name, const char *type, int32_t byteSize) {
    appendCString(out, name);
    appendCString(out, type);
    appendPod<int32_t>(out, byteSize);
}

uint16_t sanitizeToHalf(float value) {
    if (!std::isfinite(value)) return glm::packHalf1x16(0.0f);
    const float clamped = std::clamp(value, 0.0f, 65504.0f);
    return glm::packHalf1x16(clamped);
}

} // namespace

std::vector<uint8_t> encodeHalfRgbExr(uint32_t width, uint32_t height, const float *rgbaLinear, bool flipY) {
    if (width == 0 || height == 0 || rgbaLinear == nullptr) return {};

    std::vector<uint8_t> out;
    const size_t scanlineDataBytes = static_cast<size_t>(width) * 3u * sizeof(uint16_t);
    const size_t scanlineBlockBytes = sizeof(int32_t) * 2u + scanlineDataBytes;
    out.reserve(512u + static_cast<size_t>(height) * (sizeof(uint64_t) + scanlineBlockBytes));

    // OpenEXR magic (20000630) and version 2 (single-part scanline, short attribute names).
    appendPod<int32_t>(out, 20000630);
    appendPod<int32_t>(out, 2);

    // Channels attribute ("chlist"): channels must be listed in ASCII alphabetical order ("B", "G", "R").
    std::vector<uint8_t> chlist;
    for (const char *channelName : {"B", "G", "R"}) {
        appendCString(chlist, channelName);
        appendPod<int32_t>(chlist, 1); // pixelType: 1 = HALF
        appendPod<uint8_t>(chlist, 0); // pLinear = 0
        const uint8_t reserved[3] = {0, 0, 0};
        appendBytes(chlist, reserved, sizeof(reserved));
        appendPod<int32_t>(chlist, 1); // xSampling
        appendPod<int32_t>(chlist, 1); // ySampling
    }
    appendPod<uint8_t>(chlist, 0); // end of channel list
    appendAttributeHeader(out, "channels", "chlist", static_cast<int32_t>(chlist.size()));
    appendBytes(out, chlist.data(), chlist.size());

    // Compression: 0 = NO_COMPRESSION.
    appendAttributeHeader(out, "compression", "compression", 1);
    appendPod<uint8_t>(out, 0);

    // Data and display windows: [0, 0, width - 1, height - 1].
    const int32_t box[4] = {0, 0, static_cast<int32_t>(width - 1), static_cast<int32_t>(height - 1)};
    appendAttributeHeader(out, "dataWindow", "box2i", sizeof(box));
    appendBytes(out, box, sizeof(box));
    appendAttributeHeader(out, "displayWindow", "box2i", sizeof(box));
    appendBytes(out, box, sizeof(box));

    // Line order: 0 = INCREASING_Y.
    appendAttributeHeader(out, "lineOrder", "lineOrder", 1);
    appendPod<uint8_t>(out, 0);

    // Standard camera window metadata expected by OpenEXR readers.
    appendAttributeHeader(out, "pixelAspectRatio", "float", sizeof(float));
    appendPod<float>(out, 1.0f);

    const float center[2] = {0.0f, 0.0f};
    appendAttributeHeader(out, "screenWindowCenter", "v2f", sizeof(center));
    appendBytes(out, center, sizeof(center));

    appendAttributeHeader(out, "screenWindowWidth", "float", sizeof(float));
    appendPod<float>(out, 1.0f);

    // End of header.
    appendPod<uint8_t>(out, 0);

    // Line offset table: one uint64_t file offset per scanline.
    const uint64_t firstScanlineOffset =
        static_cast<uint64_t>(out.size()) + static_cast<uint64_t>(height) * sizeof(uint64_t);
    for (uint32_t y = 0; y < height; y++) {
        const uint64_t offset = firstScanlineOffset + static_cast<uint64_t>(y) * scanlineBlockBytes;
        appendPod<uint64_t>(out, offset);
    }

    // Scanline pixel blocks: int32 y, int32 byteCount, then planar B[width], G[width], R[width] in HALF.
    std::vector<uint16_t> planarRow(static_cast<size_t>(width) * 3u);
    uint16_t *bPlane = planarRow.data();
    uint16_t *gPlane = planarRow.data() + width;
    uint16_t *rPlane = planarRow.data() + static_cast<size_t>(width) * 2u;

    for (uint32_t y = 0; y < height; y++) {
        const uint32_t srcY = flipY ? (height - 1u - y) : y;
        const float *row = rgbaLinear + static_cast<size_t>(srcY) * width * 4u;
        for (uint32_t x = 0; x < width; x++) {
            const float r = row[x * 4u + 0u];
            const float g = row[x * 4u + 1u];
            const float b = row[x * 4u + 2u];
            bPlane[x] = sanitizeToHalf(b);
            gPlane[x] = sanitizeToHalf(g);
            rPlane[x] = sanitizeToHalf(r);
        }
        appendPod<int32_t>(out, static_cast<int32_t>(y));
        appendPod<int32_t>(out, static_cast<int32_t>(scanlineDataBytes));
        appendBytes(out, planarRow.data(), scanlineDataBytes);
    }

    return out;
}

std::string writeHalfRgbExrFile(const std::filesystem::path &path,
                                uint32_t width,
                                uint32_t height,
                                const float *rgbaLinear,
                                bool flipY) {
    const std::vector<uint8_t> encoded = encodeHalfRgbExr(width, height, rgbaLinear, flipY);
    if (encoded.empty()) return "empty EXR image";

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tempPath = path;
    tempPath += ".tmp";
    {
        std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
        if (!stream) return "could not open temporary EXR file for writing";
        stream.write(reinterpret_cast<const char *>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
        stream.flush();
        if (!stream) {
            std::filesystem::remove(tempPath, ec);
            return "failed to write EXR data";
        }
    }
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return "could not move temporary EXR file into place: " + ec.message();
        }
    }
    return {};
}

std::string writeRgbaPngFile(const std::filesystem::path &path, uint32_t width, uint32_t height, const uint8_t *rgba) {
    if (width == 0 || height == 0 || rgba == nullptr) return "empty PNG image";

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tempPath = path;
    tempPath += ".tmp";
    {
        struct StreamSink {
            std::ofstream stream;
            bool ok = true;
        } sink;
        sink.stream.open(tempPath, std::ios::binary | std::ios::trunc);
        if (!sink.stream) return "could not open temporary PNG file for writing";

        auto writeChunk = [](void *context, void *data, int size) {
            auto *target = static_cast<StreamSink *>(context);
            if (!target->ok || size <= 0) return;
            target->stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
            if (!target->stream) target->ok = false;
        };
        const int encoded = stbi_write_png_to_func(writeChunk, &sink, static_cast<int>(width), static_cast<int>(height),
                                                   4, rgba, static_cast<int>(width * 4));
        sink.stream.flush();
        if (encoded == 0 || !sink.ok || !sink.stream) {
            sink.stream.close();
            std::filesystem::remove(tempPath, ec);
            return "failed to write PNG data";
        }
    }
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return "could not move temporary PNG file into place: " + ec.message();
        }
    }
    return {};
}

} // namespace exr_writer
