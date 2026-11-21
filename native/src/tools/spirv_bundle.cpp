// Makes the SPIR-V bundle the game reads before compiling a shader itself (gpu::Shader::setBundle): radiante-pt's
// shaders at its default settings, compiled by the renderer's own compile (prismrtCompileSpirvBundle, in the library)
// and named as the game's cache names them, so a first start, and the first start after an update, finds them
// compiled. Run by the native build's install step with PRISM_SPIRV_BUNDLE on (the CI's release builds):
//
//   spirv_bundle <renderer library> <installed shaders folder> <variants.json> <work folder> <out zip>
//
// The installed shaders folder is the one the game unpacks (with include/ and world/ray_tracing/radiante-pt.zip); it
// is copied into the work folder, which the compile then uses as the game's folder, and removed after.

#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace fs = std::filesystem;

using CompileBundle = int (*)(const char *, const char *, char *, int);

namespace {

CompileBundle loadCompile(const fs::path &library) {
#if defined(_WIN32)
    // Its own folder first, for the libraries it needs beside it.
    SetDllDirectoryW(library.parent_path().wstring().c_str());
    HMODULE module = LoadLibraryW(library.wstring().c_str());
    if (module == nullptr) {
        std::cerr << "spirv_bundle: cannot load " << library.string() << " (error " << GetLastError() << ")"
                  << std::endl;
        return nullptr;
    }
    return reinterpret_cast<CompileBundle>(GetProcAddress(module, "prismrtCompileSpirvBundle"));
#else
    void *module = dlopen(library.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        std::cerr << "spirv_bundle: cannot load " << library.string() << ": " << dlerror() << std::endl;
        return nullptr;
    }
    return reinterpret_cast<CompileBundle>(dlsym(module, "prismrtCompileSpirvBundle"));
#endif
}

bool writeZip(const fs::path &folder, const fs::path &zip) {
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto &entry : fs::directory_iterator(folder, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".spv") files.push_back(entry.path());
    }
    if (error || files.empty()) {
        std::cerr << "spirv_bundle: nothing compiled into " << folder.string() << std::endl;
        return false;
    }
    // In name order, so the same shaders make the same zip.
    std::sort(files.begin(), files.end());
    fs::create_directories(zip.parent_path(), error);
    fs::remove(zip, error);
    void *writer = mz_zip_writer_create();
    if (writer == nullptr) return false;
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_BEST);
    bool ok = mz_zip_writer_open_file(writer, zip.string().c_str(), 0, 0) == MZ_OK;
    for (size_t i = 0; ok && i < files.size(); i++) {
        ok = mz_zip_writer_add_file(writer, files[i].string().c_str(), files[i].filename().string().c_str()) ==
             MZ_OK;
    }
    if (mz_zip_writer_close(writer) != MZ_OK) ok = false;
    mz_zip_writer_delete(&writer);
    if (!ok) {
        std::cerr << "spirv_bundle: cannot write " << zip.string() << std::endl;
        fs::remove(zip, error);
        return false;
    }
    std::cout << "spirv_bundle: " << files.size() << " shaders in " << zip.filename().string() << " ("
              << fs::file_size(zip, error) / 1024 << " KB)" << std::endl;
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: spirv_bundle <renderer library> <installed shaders folder> <variants.json> <work folder> "
                     "<out zip>"
                  << std::endl;
        return 2;
    }
    const fs::path library = fs::absolute(argv[1]);
    const fs::path shaders = fs::absolute(argv[2]);
    const fs::path variants = fs::absolute(argv[3]);
    const fs::path work = fs::absolute(argv[4]);
    const fs::path zip = fs::absolute(argv[5]);

    std::error_code error;
    fs::remove_all(work, error);
    fs::create_directories(work / "shaders/world/ray_tracing", error);
    fs::copy(shaders / "include", work / "shaders/include", fs::copy_options::recursive, error);
    if (error) {
        std::cerr << "spirv_bundle: cannot copy " << (shaders / "include").string() << ": " << error.message()
                  << std::endl;
        return 1;
    }
    fs::copy_file(shaders / "world/ray_tracing/radiante-pt.zip", work / "shaders/world/ray_tracing/radiante-pt.zip",
                  error);
    if (error) {
        std::cerr << "spirv_bundle: cannot copy radiante-pt.zip: " << error.message() << std::endl;
        return 1;
    }

    const CompileBundle compile = loadCompile(library);
    if (compile == nullptr) {
        std::cerr << "spirv_bundle: " << library.string() << " has no prismrtCompileSpirvBundle" << std::endl;
        return 1;
    }
    char message[4096] = {};
    const int compiled = compile(work.string().c_str(), variants.string().c_str(), message, sizeof(message));
    if (compiled < 0) {
        std::cerr << "spirv_bundle: " << message << std::endl;
        return 1;
    }
    const bool written = writeZip(work / "cache/shaders", zip);
    fs::remove_all(work, error);
    return written ? 0 : 1;
}
