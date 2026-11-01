#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace av {

// Reads a whole file as bytes; nullopt if it cannot be opened.
std::optional<std::string> readTextFile(const std::filesystem::path& path);

// Locates the directory that contains `marker` (a relative path such as
// "assets/materials/materials.json"), searching the current directory, the
// executable's directory and up to four parents of each. Lets the game run
// from the repo root or from inside build/.
std::optional<std::filesystem::path> findDataRoot(const char* argv0, const std::filesystem::path& marker);

// Per-user writable directory for settings and saves:
// %APPDATA%/gtascii on Windows, $XDG_CONFIG_HOME/gtascii or ~/.config/gtascii
// elsewhere. Created on first use; falls back to ./userdata.
std::filesystem::path userDataDir();

// Writes a file atomically (temp file + rename). Returns false on failure.
bool writeTextFileAtomic(const std::filesystem::path& path, const std::string& contents);

} // namespace av
