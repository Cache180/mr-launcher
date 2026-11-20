/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRMods.h"

#include <array>
#include <cstddef>
#include <exception>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace MR {

namespace {

    // Nombres fijos: nada del exterior forma parte de una ruta.
    constexpr const char* ZipName = "MRMP.zip";
    constexpr const char* ZipNameLower = "mrmp.zip";
    constexpr const char* NotFoundError = "No se encontro MRMP.zip junto a MR-Launcher.exe";

    // Compara el nombre con "mrmp.zip" ignorando mayusculas ASCII. Trabaja sobre
    // el tipo nativo (char o wchar_t) para no convertir nombres no ASCII.
    bool IsModZipName(const fs::path& Name) {
        const auto& Native = Name.native();
        const std::size_t Length = std::char_traits<char>::length(ZipNameLower);
        if (Native.size() != Length) {
            return false;
        }
        for (std::size_t I = 0; I < Length; ++I) {
            auto C = Native[I];
            if (C >= 'A' && C <= 'Z') {
                C = static_cast<decltype(C)>(C - 'A' + 'a');
            }
            if (C != static_cast<decltype(C)>(ZipNameLower[I])) {
                return false;
            }
        }
        return true;
    }

    // sha256 del archivo en hex minusculas, o "" si no se pudo leer entero.
    std::string Sha256OfFile(const fs::path& File) {
        std::ifstream In(File, std::ios::binary);
        if (!In) {
            return {};
        }
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> Ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!Ctx || EVP_DigestInit_ex(Ctx.get(), EVP_sha256(), nullptr) != 1) {
            return {};
        }
        std::array<char, 64 * 1024> Buffer {};
        while (In) {
            In.read(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
            const std::streamsize Got = In.gcount();
            if (Got > 0 && EVP_DigestUpdate(Ctx.get(), Buffer.data(), static_cast<std::size_t>(Got)) != 1) {
                return {};
            }
        }
        if (!In.eof()) {
            return {}; // error de lectura, no fin de archivo
        }
        std::array<unsigned char, EVP_MAX_MD_SIZE> Digest {};
        unsigned int DigestLength = 0;
        if (EVP_DigestFinal_ex(Ctx.get(), Digest.data(), &DigestLength) != 1) {
            return {};
        }
        static constexpr char Hex[] = "0123456789abcdef";
        std::string Out;
        Out.reserve(DigestLength * 2);
        for (unsigned int I = 0; I < DigestLength; ++I) {
            Out += Hex[Digest[I] >> 4];
            Out += Hex[Digest[I] & 0x0F];
        }
        return Out;
    }

}

void CleanMultiplayerDir(const fs::path& Dir) {
    std::error_code Ec;
    if (!fs::is_directory(Dir, Ec)) {
        return;
    }
    for (const auto& Entry : fs::directory_iterator(Dir)) {
        if (Entry.is_directory() || IsModZipName(Entry.path().filename())) {
            continue;
        }
        fs::remove(Entry.path());
    }
}

bool InstallBundledMod(const fs::path& ExeDir, const fs::path& GameUserPath, std::string& Error) {
    Error.clear();
    try {
        const fs::path Source = ExeDir / ZipName;
        std::error_code Ec;
        if (!fs::is_regular_file(Source, Ec)) {
            Error = NotFoundError;
            return false;
        }
        const std::string SourceHash = Sha256OfFile(Source);
        if (SourceHash.empty() || fs::file_size(Source) == 0) {
            Error = "No se pudo leer MRMP.zip o esta vacio";
            return false;
        }

        const fs::path TargetDir = GameUserPath / "mods" / "multiplayer";
        const fs::path Target = TargetDir / ZipName;
        if (fs::is_regular_file(Target, Ec) && Sha256OfFile(Target) == SourceHash) {
            return true; // ya instalado y sin cambios
        }

        fs::create_directories(TargetDir);
        // Copia atomica: un corte deja el zip viejo o el nuevo, nunca uno a medias.
        fs::path Temp = Target;
        Temp += ".tmp";
        fs::copy_file(Source, Temp, fs::copy_options::overwrite_existing, Ec);
        if (!Ec) {
            fs::rename(Temp, Target, Ec);
        }
        if (Ec) {
            std::error_code Ignored;
            fs::remove(Temp, Ignored);
            Error = "No se pudo instalar MRMP.zip en la carpeta del juego: " + Ec.message();
            return false;
        }
        return true;
    } catch (const std::exception& Ex) {
        Error = std::string("No se pudo instalar MRMP.zip en la carpeta del juego: ") + Ex.what();
        return false;
    }
}

}
