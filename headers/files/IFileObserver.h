#pragma once
#include <filesystem>
namespace Files
{
    class IFileObserver {
    public:
        virtual ~IFileObserver() = default;

        virtual void onFileCreated(const std::filesystem::path& path) = 0;
        virtual void onFileDeleted(const std::filesystem::path& path) = 0;
    };
}
