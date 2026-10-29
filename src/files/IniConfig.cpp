#include "files/IniConfig.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace Files {

bool IniConfig::load(const fs::path& filePath) {
    m_issues.clear();

    try {
        if (!fs::exists(filePath)) {
            std::cout << "[Config] No config found. Generating default config at: " << filePath.string() << '\n';
            populateDefaultConfig();
            return save(filePath);
        }

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            m_issues.push_back({0, "", "Could not open file for reading (permission denied or locked)", true});
            populateDefaultConfig();
            return false;
        }

        // Read entire content to handle Windows Notepad UTF-8 BOM
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Strip UTF-8 BOM (\xEF\xBB\xBF) if present
        if (content.size() >= 3 &&
            static_cast<unsigned char>(content[0]) == 0xEF &&
            static_cast<unsigned char>(content[1]) == 0xBB &&
            static_cast<unsigned char>(content[2]) == 0xBF)
        {
            content.erase(0, 3);
        }

        m_data.clear();
        std::istringstream stream(content);
        std::string line;
        std::string currentSection = "General";
        size_t currentLineNum = 0;

        while (std::getline(stream, line)) {
            currentLineNum++;
            std::string trimmed = trim(line);

            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
                continue; // Skip comments and blank lines
            }

            // Section Header: [SectionName]
            if (trimmed.front() == '[') {
                if (trimmed.back() != ']') {
                    m_issues.push_back({currentLineNum, line, "Malformed section header: missing closing ']'", false});
                    continue;
                }
                currentSection = trim(trimmed.substr(1, trimmed.size() - 2));
                if (currentSection.empty()) {
                    m_issues.push_back({currentLineNum, line, "Empty section header '[]'", false});
                    currentSection = "General";
                }
            }
            // Key-Value Pair: key = value
            else {
                size_t eqPos = trimmed.find('=');
                if (eqPos == std::string::npos) {
                    m_issues.push_back({currentLineNum, line, "Syntax error: Missing '=' delimiter", false});
                    continue;
                }

                std::string key = trim(trimmed.substr(0, eqPos));
                std::string val = trim(trimmed.substr(eqPos + 1));

                if (key.empty()) {
                    m_issues.push_back({currentLineNum, line, "Empty key name before '='", false});
                    continue;
                }

                m_data[currentSection][key] = val;
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        m_issues.push_back({0, "", std::string("Unexpected exception while parsing config: ") + e.what(), true});
        populateDefaultConfig();
        return false;
    }
}

bool IniConfig::save(const fs::path& filePath) const {
    try {
        // Ensure parent directory exists
        if (filePath.has_parent_path() && !fs::exists(filePath.parent_path())) {
            fs::create_directories(filePath.parent_path());
        }

        std::ofstream file(filePath, std::ios::trunc);
        if (!file.is_open()) return false;

        file << "; Keychron K2 HE Native Daemon Configuration\n";
        file << "; Hotkeys format: [Modifier+]Key (e.g. Ctrl+Shift+], Ctrl+Alt+N, F9)\n\n";

        for (const auto& [section, keys] : m_data) {
            file << "[" << section << "]\n";
            for (const auto& [k, v] : keys) {
                file << k << " = " << v << "\n";
            }
            file << "\n";
        }
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[Config] Failed to write config.ini: " << e.what() << '\n';
        return false;
    }
}

std::string IniConfig::get(const std::string& section, const std::string& key, const std::string& defaultVal) const {
    if (auto sIt = m_data.find(section); sIt != m_data.end()) {
        if (auto kIt = sIt->second.find(key); kIt != sIt->second.end()) {
            return kIt->second;
        }
    }
    return defaultVal;
}

void IniConfig::set(const std::string& section, const std::string& key, const std::string& value) {
    m_data[section][key] = value;
}

HotkeyParseResult IniConfig::parseHotkey(const std::string& hotkeyStr) {
    HotkeyParseResult result;
    result.binding.rawString = hotkeyStr;

    std::string trimmed = trim(hotkeyStr);
    if (trimmed.empty()) {
        result.errorMessage = "Hotkey string is empty.";
        return result;
    }

#if defined(_WIN32)
    std::stringstream ss(trimmed);
    std::string token;
    result.binding.modifiers = MOD_NOREPEAT;
    bool baseKeyFound = false;

    while (std::getline(ss, token, '+')) {
        std::string t = trim(token);
        if (t.empty()) continue;

        std::string upper = t;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        if (upper == "CTRL" || upper == "CONTROL") {
            result.binding.modifiers |= MOD_CONTROL;
        } else if (upper == "SHIFT") {
            result.binding.modifiers |= MOD_SHIFT;
        } else if (upper == "ALT") {
            result.binding.modifiers |= MOD_ALT;
        } else if (upper == "WIN" || upper == "SUPER") {
            result.binding.modifiers |= MOD_WIN;
        } else {
            // Found base key
            if (baseKeyFound) {
                result.errorMessage = "Multiple base keys specified: '" + t + "' after already finding base key.";
                return result;
            }

            if (t == "[") result.binding.vkCode = VK_OEM_4;
            else if (t == "]") result.binding.vkCode = VK_OEM_6;
            else if (t == "\\") result.binding.vkCode = VK_OEM_5;
            else if (t == "-" || t == "_") result.binding.vkCode = VK_OEM_MINUS;
            else if (t == "=" || t == "+") result.binding.vkCode = VK_OEM_PLUS;
            else if (t == ";" || t == ":") result.binding.vkCode = VK_OEM_1;
            else if (t == "'" || t == "\"") result.binding.vkCode = VK_OEM_7;
            else if (t == "/" || t == "?") result.binding.vkCode = VK_OEM_2;
            else if (t == "`" || t == "~") result.binding.vkCode = VK_OEM_3;
            else if (upper.rfind("F", 0) == 0 && upper.size() >= 2 && std::isdigit(upper[1])) {
                try {
                    int fNum = std::stoi(upper.substr(1));
                    if (fNum >= 1 && fNum <= 12) {
                        result.binding.vkCode = VK_F1 + (fNum - 1);
                    } else {
                        result.errorMessage = "Unsupported Function key '" + t + "' (supports F1-F12).";
                        return result;
                    }
                } catch (...) {
                    result.errorMessage = "Invalid Function key format '" + t + "'.";
                    return result;
                }
            } else if (t.size() == 1) {
                char c = upper[0];
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    result.binding.vkCode = c;
                } else {
                    result.errorMessage = "Unsupported character key: '" + t + "'.";
                    return result;
                }
            } else {
                result.errorMessage = "Unrecognized key or modifier token: '" + t + "'.";
                return result;
            }

            baseKeyFound = true;
        }
    }

    if (!baseKeyFound) {
        result.errorMessage = "No base key provided (modifiers only: '" + hotkeyStr + "').";
        return result;
    }

    result.isValid = (result.binding.vkCode != 0);
    return result;
#else
    // Linux/POSIX Stub (handled via IPC)
    result.isValid = true;
    return result;
#endif
}

bool IniConfig::hasErrors() const noexcept {
    for (const auto& issue : m_issues) {
        if (issue.isFatal) return true;
    }
    return false;
}

void IniConfig::populateDefaultConfig() {
    set("Hotkeys", "next_profile", "Ctrl+Shift+]");
    set("Hotkeys", "prev_profile", "Ctrl+Shift+[");
    set("Hotkeys", "slot_1", "Ctrl+Alt+1");
    set("Hotkeys", "slot_2", "Ctrl+Alt+2");
    set("Hotkeys", "slot_3", "Ctrl+Alt+3");
    set("Settings", "auto_reconnect", "true");
}

std::string IniConfig::trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(first, (last - first + 1)));
}

} // namespace files