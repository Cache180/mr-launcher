/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRMods.h"
#include "test_support.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <set>
#include <string>

namespace fs = std::filesystem;
using mrtest::ReadAll;
using mrtest::TempDir;
using mrtest::WriteAll;

namespace {

constexpr const char* NotFound = "No se encontro MRMP.zip junto a MR-Launcher.exe";

// Nombres (archivos y carpetas) que hay dentro de Dir.
std::set<std::string> Names(const fs::path& Dir) {
    std::set<std::string> Out;
    for (const auto& Entry : fs::directory_iterator(Dir)) {
        Out.insert(Entry.path().filename().string());
    }
    return Out;
}

// Un ExeDir con MRMP.zip y un GameUserPath vacio dentro del mismo directorio temporal.
struct Layout {
    TempDir Root;
    fs::path ExeDir;
    fs::path GameDir;
    fs::path Installed; // GameDir/mods/multiplayer/MRMP.zip

    Layout() {
        ExeDir = Root.Path() / "exe";
        GameDir = Root.Path() / "game";
        Installed = GameDir / "mods" / "multiplayer" / "MRMP.zip";
        fs::create_directories(ExeDir);
        fs::create_directories(GameDir);
    }
};

} // namespace

TEST_CASE("MR::CleanMultiplayerDir: deja solo mrmp.zip y las subcarpetas") {
    TempDir Dir;
    WriteAll(Dir.Path() / "BeamMP.zip", "a");
    WriteAll(Dir.Path() / "MRMP.zip", "b");
    WriteAll(Dir.Path() / "otro.zip", "c");
    fs::create_directory(Dir.Path() / "subcarpeta");
    WriteAll(Dir.Path() / "subcarpeta" / "dentro.zip", "d");

    REQUIRE_NOTHROW(MR::CleanMultiplayerDir(Dir.Path()));

    CHECK(Names(Dir.Path()) == std::set<std::string> { "MRMP.zip", "subcarpeta" });
    CHECK(ReadAll(Dir.Path() / "MRMP.zip") == "b");
    CHECK(fs::exists(Dir.Path() / "subcarpeta" / "dentro.zip")); // no toca subcarpetas
}

TEST_CASE("MR::CleanMultiplayerDir: mrmp.zip se reconoce sin distinguir mayusculas") {
    for (const char* Name : { "mrmp.zip", "MRMP.ZIP", "MrMp.Zip" }) {
        CAPTURE(Name);
        TempDir Dir;
        WriteAll(Dir.Path() / Name, "x");
        WriteAll(Dir.Path() / "mrmp.zip.bak", "y");
        WriteAll(Dir.Path() / "xmrmp.zip", "z");
        MR::CleanMultiplayerDir(Dir.Path());
        CHECK(Names(Dir.Path()) == std::set<std::string> { Name });
    }
}

TEST_CASE("MR::CleanMultiplayerDir: un directorio inexistente o vacio no es un error") {
    TempDir Dir;
    CHECK_NOTHROW(MR::CleanMultiplayerDir(Dir.Path() / "no-existe"));
    CHECK_FALSE(fs::exists(Dir.Path() / "no-existe")); // tampoco lo crea
    CHECK_NOTHROW(MR::CleanMultiplayerDir(Dir.Path()));
    CHECK(Names(Dir.Path()).empty());
}

TEST_CASE("MR::InstallBundledMod: sin MRMP.zip devuelve falso con el mensaje exacto") {
    Layout L;
    std::string Error = "previo";
    CHECK_FALSE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK(Error == NotFound);
    CHECK_FALSE(fs::exists(L.Installed));
}

TEST_CASE("MR::InstallBundledMod: una carpeta llamada MRMP.zip no cuenta como el zip") {
    Layout L;
    fs::create_directory(L.ExeDir / "MRMP.zip");
    std::string Error;
    CHECK_FALSE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK(Error == NotFound);
}

TEST_CASE("MR::InstallBundledMod: un MRMP.zip vacio se rechaza") {
    Layout L;
    WriteAll(L.ExeDir / "MRMP.zip", "");
    std::string Error;
    CHECK_FALSE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK_FALSE(Error.empty());
    CHECK_FALSE(fs::exists(L.Installed));
}

TEST_CASE("MR::InstallBundledMod: copia el zip creando mods/multiplayer") {
    Layout L;
    WriteAll(L.ExeDir / "MRMP.zip", "contenido del mod");
    std::string Error = "previo";
    REQUIRE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK(Error.empty());
    REQUIRE(fs::is_regular_file(L.Installed));
    CHECK(ReadAll(L.Installed) == "contenido del mod");
    CHECK(Names(L.Installed.parent_path()) == std::set<std::string> { "MRMP.zip" }); // sin temporales
}

TEST_CASE("MR::InstallBundledMod: sin cambios no reescribe el destino") {
    Layout L;
    WriteAll(L.ExeDir / "MRMP.zip", "contenido del mod");
    std::string Error;
    REQUIRE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));

    // Se envejece el destino: si la segunda llamada lo reescribiera, el mtime cambiaria.
    const auto Old = fs::last_write_time(L.Installed) - std::chrono::hours(2);
    fs::last_write_time(L.Installed, Old);

    REQUIRE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK(Error.empty());
    CHECK(fs::last_write_time(L.Installed) == Old);
    CHECK(ReadAll(L.Installed) == "contenido del mod");
}

TEST_CASE("MR::InstallBundledMod: un destino distinto se reemplaza") {
    Layout L;
    WriteAll(L.ExeDir / "MRMP.zip", "version nueva");
    fs::create_directories(L.Installed.parent_path());
    WriteAll(L.Installed, "version vieja y mas larga");
    const auto Old = fs::last_write_time(L.Installed) - std::chrono::hours(2);
    fs::last_write_time(L.Installed, Old);

    std::string Error;
    REQUIRE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK(ReadAll(L.Installed) == "version nueva");
    CHECK(fs::last_write_time(L.Installed) != Old);
    CHECK(Names(L.Installed.parent_path()) == std::set<std::string> { "MRMP.zip" });
}

TEST_CASE("MR::InstallBundledMod: mismo tamano y distinto contenido tambien se reemplaza") {
    Layout L;
    WriteAll(L.ExeDir / "MRMP.zip", "AAAA");
    fs::create_directories(L.Installed.parent_path());
    WriteAll(L.Installed, "BBBB");
    std::string Error;
    REQUIRE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK(ReadAll(L.Installed) == "AAAA");
}

TEST_CASE("MR::InstallBundledMod: si no puede instalar devuelve falso con motivo y no deja restos") {
    Layout L;
    WriteAll(L.ExeDir / "MRMP.zip", "contenido");
    // Un archivo ocupa el lugar de mods/: no se puede crear mods/multiplayer.
    WriteAll(L.GameDir / "mods", "no soy una carpeta");
    std::string Error;
    CHECK_FALSE(MR::InstallBundledMod(L.ExeDir, L.GameDir, Error));
    CHECK_FALSE(Error.empty());
    CHECK(Error != NotFound);
    CHECK(ReadAll(L.GameDir / "mods") == "no soy una carpeta");
}
