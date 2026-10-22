// mdhtml <file.md>: the Markdown file as HTML on standard output.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "document.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

std::optional<std::string> ReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// Standard output in binary mode, so that a line ends with "\n" on every system.
void WriteOutput(const std::string& text) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::fwrite(text.data(), 1, text.size(), stdout);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mdhtml <file.md>\n";
        return kExitUsage;
    }
    std::optional<std::string> text = ReadFile(argv[1]);
    if (!text) {
        std::cerr << "error: cannot read " << argv[1] << "\n";
        return kExitError;
    }
    WriteOutput(mdhtml::RenderDocument(*text));
    return 0;
}
