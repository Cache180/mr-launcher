/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRPaths.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("MR::ResolveCacheDir: una ruta relativa cuelga de la carpeta del exe, tambien con caracteres no ANSI") {
    const fs::path Exe = L"C:/\u00D1and\u00FA \u65E5\u672C/MR";
    CHECK(MR::ResolveCacheDir(Exe, "./Resources") == Exe / L"Resources");
    CHECK(MR::ResolveCacheDir(Exe, "Resources") == Exe / L"Resources");
    CHECK(MR::ResolveCacheDir(Exe, "mods/cache") == Exe / L"mods" / L"cache");
}

TEST_CASE("MR::ResolveCacheDir: una ruta absoluta se respeta") {
#ifdef _WIN32
    const fs::path Abs = L"D:/Juegos/MR cache";
#else
    const fs::path Abs = "/srv/mr cache";
#endif
    CHECK(MR::ResolveCacheDir(fs::path(L"C:/MR"), Abs.string()) == Abs);
}

TEST_CASE("MR::ResolveCacheDir: la cadena vacia es ExeDir/Resources") {
    const fs::path Exe = L"C:/MR";
    CHECK(MR::ResolveCacheDir(Exe, "") == Exe / L"Resources");
}

TEST_CASE("MR::ResolveCacheDir: una ruta con raiz pero no absoluta no escapa de la carpeta del exe") {
    const fs::path Exe = L"C:/MR";
    CHECK(MR::ResolveCacheDir(Exe, "/cache") == Exe / L"cache");
#ifdef _WIN32
    CHECK(MR::ResolveCacheDir(Exe, "\\cache") == Exe / L"cache");
#endif
}

TEST_CASE("MR::ResolveCacheDir: no depende del directorio de trabajo") {
    const fs::path Exe = L"C:/MR";
    const fs::path Before = fs::current_path();
    const fs::path Resolved = MR::ResolveCacheDir(Exe, "./Resources");
    CHECK(fs::current_path() == Before);
    CHECK(Resolved.is_absolute());
}

TEST_CASE("MR::DefaultCacheDir: es Resources") {
    CHECK(std::string(MR::DefaultCacheDir) == "Resources");
}
