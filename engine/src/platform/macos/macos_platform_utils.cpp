#ifdef HN_PLATFORM_MACOS
#include "hnpch.h"
#include "Honey/utils/platform_utils.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib> // free

// Bridge functions implemented in macos_file_dialog_bridge.mm
extern "C" {
    // Returns a heap-allocated C string (UTF-8) with the selected path, or nullptr if cancelled.
    // The caller must free() the returned pointer.
    const char* hn_mac_open_file(const char* extensions_csv);
    const char* hn_mac_save_file(const char* extensions_csv);
}

namespace {

// Parse a Windows-style filter string into a list of file extensions.
static std::vector<std::string> parse_extensions_from_filter(const char* filter) {
    std::vector<std::string> extensions;
    if (!filter || !*filter) return extensions;

    const char* p = filter;
    while (*p) {
        // Skip description
        while (*p) ++p;
        ++p; // move past NUL

        if (!*p) break; // no pattern following

        // Pattern token (e.g., "*.png;*.jpg")
        const char* pattern_start = p;
        while (*p) ++p;
        std::string pattern(pattern_start, p);

        // Split by ';'
        size_t start = 0;
        while (start < pattern.size()) {
            size_t end = pattern.find(';', start);
            if (end == std::string::npos) end = pattern.size();
            std::string item = pattern.substr(start, end - start);
            // Trim spaces
            item.erase(0, item.find_first_not_of(" \t"));
            if (!item.empty())
                item.erase(item.find_last_not_of(" \t") + 1);

            if (!item.empty()) {
                if (item[0] == '*') item.erase(0, 1);
                if (!item.empty() && item[0] == '.') item.erase(0, 1);

                if (!item.empty() && item.find_first_of("\\/*?") == std::string::npos) {
                    std::transform(item.begin(), item.end(), item.begin(),
                                   [](unsigned char c){ return (char)std::tolower(c); });
                    if (std::find(extensions.begin(), extensions.end(), item) == extensions.end()) {
                        extensions.push_back(item);
                    }
                }
            }

            start = end + (end < pattern.size() ? 1 : 0);
        }

        ++p; // move past NUL
    }

    return extensions;
}

// Join extensions into a comma-separated string (e.g., "png,jpg,jpeg")
static std::string join_extensions_csv(const std::vector<std::string>& exts) {
    if (exts.empty()) return std::string();
    std::string out;
    out.reserve(exts.size() * 4);
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i) out += ',';
        out += exts[i];
    }
    return out;
}

} // namespace

namespace Honey {

    std::string FileDialogs::open_file(const char* filter) {
        auto exts = parse_extensions_from_filter(filter);
        std::string csv = join_extensions_csv(exts);

        const char* result = hn_mac_open_file(csv.empty() ? nullptr : csv.c_str());
        if (!result) return std::string();

        std::string path(result);
        std::free((void*)result);
        return path;
    }

    std::string FileDialogs::save_file(const char* filter) {
        auto exts = parse_extensions_from_filter(filter);
        std::string csv = join_extensions_csv(exts);

        const char* result = hn_mac_save_file(csv.empty() ? nullptr : csv.c_str());
        if (!result) return std::string();

        std::string path(result);
        std::free((void*)result);
        return path;
    }

} // namespace Honey

#endif