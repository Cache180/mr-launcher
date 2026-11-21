/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRPaths.h"

#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace MR {

namespace {

    // Ruta del ejecutable en ejecucion, o vacia si no se puede determinar.
    fs::path ExePath() {
#if defined(_WIN32)
        // El tamano se pasa en caracteres (no en bytes). 0 es fallo y un resultado igual al
        // tamano es ruta truncada.
        std::vector<wchar_t> Buffer(32768);
        const DWORD Len = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
        if (Len == 0 || Len >= Buffer.size()) {
            return {};
        }
        return fs::path(Buffer.data());
#elif defined(__linux__)
        std::error_code Ec;
        fs::path Path = fs::canonical("/proc/self/exe", Ec);
        return Ec ? fs::path() : Path;
#elif defined(__APPLE__)
        char Buffer[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(getpid(), Buffer, sizeof(Buffer)) <= 0) {
            return {};
        }
        return fs::path(Buffer);
#else
        return {};
#endif
    }

}

fs::path ExeDir() {
    try {
        const fs::path Exe = ExePath();
        if (Exe.empty()) {
            return {};
        }
        std::error_code Ec;
        fs::path Dir = fs::weakly_canonical(Exe.parent_path(), Ec);
        return Ec ? Exe.parent_path() : Dir;
    } catch (...) {
        return {};
    }
}

fs::path ResolveCacheDir(const fs::path& Dir, const std::string& Configured) {
    // u8path: el texto del cfg es UTF-8 (path(std::string) usaria la pagina ANSI).
    const fs::path Wanted = Configured.empty() ? fs::path(DefaultCacheDir) : fs::u8path(Configured);
    if (Wanted.is_absolute()) {
        return Wanted;
    }
    // Una raiz sin ser absoluta ("/x", "C:x") no puede escapar de la carpeta del exe.
    return Dir / (Wanted.has_root_name() || Wanted.has_root_directory() ? Wanted.relative_path() : Wanted);
}

}
