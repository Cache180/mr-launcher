/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Ayudas comunes de las pruebas: directorio temporal unico y E/S de archivos.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>

namespace mrtest {

// Directorio temporal unico, borrado al salir del alcance.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> Counter { 0 };
        std::ostringstream Id;
        Id << "mr-launcher-test-" << std::random_device {}() << '-'
           << std::chrono::steady_clock::now().time_since_epoch().count() << '-' << Counter++;
        mPath = std::filesystem::temp_directory_path() / Id.str();
        std::filesystem::create_directories(mPath);
    }
    ~TempDir() {
        std::error_code Ec;
        std::filesystem::remove_all(mPath, Ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::filesystem::path Path() const { return mPath; }
    std::filesystem::path Cfg() const { return mPath / "MR-Launcher.cfg"; }
    std::filesystem::path Tmp() const { return mPath / "MR-Launcher.cfg.tmp"; }

private:
    std::filesystem::path mPath;
};

inline void WriteAll(const std::filesystem::path& File, const std::string& Content) {
    std::ofstream Out(File, std::ios::binary | std::ios::trunc);
    Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
}

inline std::string ReadAll(const std::filesystem::path& File) {
    std::ifstream In(File, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>());
}

}
