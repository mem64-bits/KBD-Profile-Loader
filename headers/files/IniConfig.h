#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <cstdint>
#include <string_view>

namespace fs = std::filesystem;

namespace Files {

    struct HotkeyBinding {
        uint32_t    modifiers = 0; // Win32 MOD_CONTROL, MOD_SHIFT, etc.
        uint32_t    vkCode    = 0; // Win32 Virtual Key
        std::string rawString;     // Original string representation
    };

    struct HotkeyParseResult {
        bool          isValid = false;
        HotkeyBinding binding;
        std::string   errorMessage;
    };

    struct ConfigIssue {
        size_t      lineNumber = 0;
        std::string lineContent;
        std::string message;
        bool        isFatal = false;
    };

    class IniConfig {
    public:
        IniConfig() = default;

        // Exception-safe loading and syntax validation
        bool load(const fs::path& filePath);
        bool save(const fs::path& filePath) const;

        [[nodiscard]] std::string get(const std::string& section, const std::string& key, const std::string& defaultVal = "") const;
        void set(const std::string& section, const std::string& key, const std::string& value);

        // Syntax-checked hotkey parser with granular error reporting
        [[nodiscard]] static HotkeyParseResult parseHotkey(const std::string& hotkeyStr);

        [[nodiscard]] const std::vector<ConfigIssue>& getIssues() const noexcept { return m_issues; }
        [[nodiscard]] bool hasErrors() const noexcept;

    private:
        void populateDefaultConfig();
        static std::string trim(std::string_view str);

        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_data;
        std::vector<ConfigIssue> m_issues;
    };

} // namespace files