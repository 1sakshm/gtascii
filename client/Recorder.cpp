#include "Recorder.h"

#include <ctime>

namespace client {

std::string Recorder::start(const std::filesystem::path& directory, int cols, int rows)
{
    stop();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::time_t now = std::time(nullptr);
    char name[64];
    std::strftime(name, sizeof name, "gtascii-%Y%m%d-%H%M%S.cast", std::localtime(&now));
    const std::filesystem::path path = directory / name;
    file_ = std::fopen(path.string().c_str(), "wb");
    if (!file_) {
        return "";
    }
    std::fprintf(file_,
                 "{\"version\": 2, \"width\": %d, \"height\": %d, \"timestamp\": %lld, \"title\": \"Grand Theft ASCII\", "
                 "\"env\": {\"TERM\": \"xterm-256color\"}}\n",
                 cols, rows, static_cast<long long>(now));
    start_ = std::chrono::steady_clock::now();
    return path.string();
}

void Recorder::stop()
{
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

double Recorder::seconds() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

void Recorder::write(std::string_view bytes)
{
    if (!file_ || bytes.empty()) {
        return;
    }
    // [time, "o", "escaped output"]
    std::string escaped;
    escaped.reserve(bytes.size() + bytes.size() / 4);
    for (const char ch : bytes) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            escaped += buf;
        } else {
            escaped += ch; // UTF-8 passes through unchanged
        }
    }
    std::fprintf(file_, "[%.4f, \"o\", \"%s\"]\n", seconds(), escaped.c_str());
}

} // namespace client
