#include "Core/FileUtil.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace av {

std::optional<std::string> readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::optional<std::filesystem::path> findDataRoot(const char* argv0, const std::filesystem::path& marker)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> starts;
    std::error_code ec;
    starts.push_back(fs::current_path(ec));
    if (argv0 != nullptr && *argv0 != '\0') {
        fs::path exe = fs::absolute(fs::path(argv0), ec);
        if (!ec) {
            starts.push_back(exe.parent_path());
        }
    }

    for (fs::path dir : starts) {
        for (int up = 0; up <= 4 && !dir.empty(); ++up) {
            if (fs::exists(dir / marker, ec)) {
                return dir;
            }
            const fs::path parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    }
    return std::nullopt;
}

std::filesystem::path userDataDir()
{
    namespace fs = std::filesystem;
    fs::path dir;
    if (const char* override = std::getenv("GTASCII_HOME"); override && *override) {
        dir = override;
    } else {
#ifdef _WIN32
        if (const char* appData = std::getenv("APPDATA"); appData && *appData) {
            dir = fs::path(appData) / "gtascii";
        }
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
            dir = fs::path(xdg) / "gtascii";
        } else if (const char* home = std::getenv("HOME"); home && *home) {
            dir = fs::path(home) / ".config" / "gtascii";
        }
#endif
    }
    if (dir.empty()) {
        dir = fs::current_path() / "userdata";
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

bool writeTextFileAtomic(const std::filesystem::path& path, const std::string& contents)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << contents;
        if (!out) {
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
    return !ec;
}

} // namespace av
