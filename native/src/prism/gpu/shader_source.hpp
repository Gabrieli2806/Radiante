#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

// Reading a shader's source a line at a time, for what the compiler would make of it: its code without comments and
// the files it includes. The dependency hash of the shader cache (gpu/shader.cpp) and the shader pack's scan for the
// settings a shader names (shader_pack/shader_compile.cpp) both read sources this way, and must agree on it.
namespace gpu::shader_source {

inline bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

inline bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

inline bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The code of a line without its comments, in `code`. `inBlockComment` carries a /* */ comment from line to line.
inline void stripComments(std::string_view line, bool &inBlockComment, std::string &code) {
    code.clear();
    size_t i = 0;
    while (i < line.size()) {
        if (inBlockComment) {
            const size_t end = line.find("*/", i);
            if (end == std::string_view::npos) { return; }
            inBlockComment = false;
            i = end + 2;
            continue;
        }
        const size_t slash = line.find('/', i);
        if (slash == std::string_view::npos || slash + 1 == line.size()) {
            code.append(line.substr(i));
            return;
        }
        if (line[slash + 1] == '*') {
            code.append(line.substr(i, slash - i));
            inBlockComment = true;
            i = slash + 2;
        } else if (line[slash + 1] == '/') {
            code.append(line.substr(i, slash - i));
            return;
        } else {
            code.append(line.substr(i, slash + 1 - i));
            i = slash + 1;
        }
    }
}

// The file an #include line names. The compiler looks for a name in quotes next to the including file first, then in
// the include directories; for one in angle brackets (radiante/v1/...), only in the include directories.
struct IncludeLine {
    std::string_view name;
    bool angled = false;
};

inline std::optional<IncludeLine> includeLine(std::string_view line) {
    size_t i = 0;
    const auto skipSpace = [&] {
        while (i < line.size() && isSpace(line[i])) { i++; }
    };
    skipSpace();
    if (i == line.size() || line[i] != '#') { return std::nullopt; }
    i++;
    skipSpace();
    const size_t directive = i;
    while (i < line.size() && isIdentifierChar(line[i])) { i++; }
    if (line.substr(directive, i - directive) != "include") { return std::nullopt; }
    skipSpace();
    if (i == line.size() || (line[i] != '"' && line[i] != '<')) { return std::nullopt; }
    const bool angled = line[i] == '<';
    const size_t start = i + 1;
    const size_t end = line.find(angled ? '>' : '"', start);
    if (end == std::string_view::npos) { return std::nullopt; }
    return IncludeLine{line.substr(start, end - start), angled};
}

} // namespace gpu::shader_source
