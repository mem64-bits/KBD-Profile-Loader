#pragma once

#include <string>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

struct KeyboardProfile {
    std::string           m_name{};
    std::filesystem::path m_filePath{};
    nlohmann::json        m_configData{};

    KeyboardProfile() = default;
    KeyboardProfile(std::string name, std::filesystem::path path, nlohmann::json data)
        : m_name(std::move(name)),
          m_filePath(std::move(path)),
          m_configData(std::move(data))
    {}

    // Required by dsa::DLL for contains() and remove()
    bool operator==(const KeyboardProfile& other) const noexcept {
        return m_name == other.m_name;
    }

    friend std::ostream& operator<<(std::ostream& os, const KeyboardProfile& profile) {
        return os << "[Profile] " << profile.m_name
                  << " (" << profile.m_filePath.filename().string() << ")";
    }
};