#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace client {

// Records the exact terminal output stream to an asciinema v2 ".cast" file
// (https://docs.asciinema.org/manual/asciicast/v2/). Play it back with
// `asciinema play file.cast`, upload it, or turn it into a GIF with `agg`.
class Recorder {
public:
    ~Recorder() { stop(); }

    // Starts a new recording; returns the file path ("" on failure).
    std::string start(const std::filesystem::path& directory, int cols, int rows);
    void stop();
    bool active() const { return file_ != nullptr; }
    void write(std::string_view bytes);
    double seconds() const;

private:
    std::FILE* file_ = nullptr;
    std::chrono::steady_clock::time_point start_;
};

} // namespace client
