/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRIdentity.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const char* NameReason = "nombre invalido (3-20 caracteres: letras, numeros, _ . -)";

// Directorio temporal unico, borrado al salir del alcance.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> Counter { 0 };
        std::ostringstream Id;
        Id << "mr-launcher-test-" << std::random_device {}() << '-'
           << std::chrono::steady_clock::now().time_since_epoch().count() << '-' << Counter++;
        mPath = fs::temp_directory_path() / Id.str();
        fs::create_directories(mPath);
    }
    ~TempDir() {
        std::error_code Ec;
        fs::remove_all(mPath, Ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    fs::path Cfg() const { return mPath / "MR-Launcher.cfg"; }
    fs::path Tmp() const { return mPath / "MR-Launcher.cfg.tmp"; }

private:
    fs::path mPath;
};

void WriteAll(const fs::path& File, const std::string& Content) {
    std::ofstream Out(File, std::ios::binary | std::ios::trunc);
    Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
}

std::string ReadAll(const fs::path& File) {
    std::ifstream In(File, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>());
}

nlohmann::json ReadJson(const fs::path& File) {
    return nlohmann::json::parse(ReadAll(File), nullptr, false);
}

bool IsHex48(const std::string& S) {
    if (S.size() != 48) {
        return false;
    }
    for (char C : S) {
        if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) {
            return false;
        }
    }
    return true;
}

// El archivo en disco es un cfg valido y completo.
void CheckValidCfg(const fs::path& File) {
    nlohmann::json D = ReadJson(File);
    REQUIRE(D.is_object());
    CHECK(D["Port"].is_number_integer());
    CHECK(D["Build"].is_string());
    CHECK(D["Name"].is_string());
    CHECK(D["Install"].is_string());
    CHECK(MR::IsValidInstallKey(D["Install"].get<std::string>()));
    CHECK(D["Servers"].is_array());
    CHECK_FALSE(fs::exists(File.string() + ".tmp"));
}

const std::string ValidInstall(48, 'a');

} // namespace

TEST_CASE("MR::IsValidPilotName") {
    CHECK(MR::IsValidPilotName("abc"));
    CHECK(MR::IsValidPilotName("Jesus.211_cache-1")); // 17
    CHECK(MR::IsValidPilotName(std::string(20, 'a')));
    CHECK_FALSE(MR::IsValidPilotName("ab"));
    CHECK_FALSE(MR::IsValidPilotName(std::string(21, 'a')));
    CHECK_FALSE(MR::IsValidPilotName("con espacio"));
    CHECK_FALSE(MR::IsValidPilotName("ni\xC3\xB1o")); // enie en UTF-8
    CHECK_FALSE(MR::IsValidPilotName("a\nb"));
    CHECK_FALSE(MR::IsValidPilotName(""));
}

TEST_CASE("MR::GenerateInstallKey") {
    std::string A = MR::GenerateInstallKey();
    std::string B = MR::GenerateInstallKey();
    CHECK(IsHex48(A));
    CHECK(IsHex48(B));
    CHECK(A != B);
}

TEST_CASE("MR::LoadOrCreateIdentity: archivo inexistente se crea") {
    TempDir Dir;
    MR::LauncherIdentity Id;
    REQUIRE_NOTHROW(Id = MR::LoadOrCreateIdentity(Dir.Cfg()));
    CHECK(IsHex48(Id.Install));
    CHECK(Id.Name.empty());
    REQUIRE(fs::exists(Dir.Cfg()));
    CheckValidCfg(Dir.Cfg());
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Port"] == 4464);
    CHECK(D["Servers"].empty());
    CHECK(D["Install"] == Id.Install);
}

TEST_CASE("MR::LoadOrCreateIdentity: segunda llamada devuelve el mismo Install y no reescribe") {
    TempDir Dir;
    MR::LauncherIdentity First = MR::LoadOrCreateIdentity(Dir.Cfg());
    std::string Before = ReadAll(Dir.Cfg());
    MR::LauncherIdentity Second = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(First.Install == Second.Install);
    CHECK(ReadAll(Dir.Cfg()) == Before);
}

TEST_CASE("MR::LoadOrCreateIdentity: archivo corrupto no lanza y se repara") {
    static const char Binary[] = "\x00\xFF\xFE\x80\x01\x02" "garbage\xC3\x28";
    const std::string Bad[] = {
        "",
        "{",
        std::string(Binary, sizeof(Binary) - 1), // incluye el NUL inicial

        "[1,2,3]",
        "42",
        "null",
        "{\"Install\": 5, \"Name\": [], \"Port\": null, \"Servers\": \"x\", \"Build\": 7}",
    };
    for (const std::string& Content : Bad) {
        CAPTURE(Content);
        TempDir Dir;
        WriteAll(Dir.Cfg(), Content);
        MR::LauncherIdentity Id;
        REQUIRE_NOTHROW(Id = MR::LoadOrCreateIdentity(Dir.Cfg()));
        CHECK(IsHex48(Id.Install));
        CHECK(Id.Name.empty());
        CheckValidCfg(Dir.Cfg());
        CHECK(ReadJson(Dir.Cfg())["Port"] == 4464);
        CHECK(ReadJson(Dir.Cfg())["Install"] == Id.Install);
    }
}

TEST_CASE("MR::LoadOrCreateIdentity: Install valido con Name invalido conserva Install") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Port":4464,"Build":"Default","Name":"a b","Install":")" + ValidInstall + R"("})");
    MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(Id.Install == ValidInstall);
    CHECK(Id.Name.empty());
    CheckValidCfg(Dir.Cfg());
    CHECK(ReadJson(Dir.Cfg())["Install"] == ValidInstall);
    CHECK(ReadJson(Dir.Cfg())["Name"] == "");
}

TEST_CASE("MR::LoadOrCreateIdentity: Install valido con Port invalido conserva Install y repara Port") {
    const char* BadPorts[] = { R"("texto")", "0", "70000", "-5", "44.5", "null", "[]" };
    for (const char* BadPort : BadPorts) {
        CAPTURE(BadPort);
        TempDir Dir;
        WriteAll(Dir.Cfg(), std::string(R"({"Port":)") + BadPort + R"(,"Install":")" + ValidInstall + R"("})");
        MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
        CHECK(Id.Install == ValidInstall);
        CheckValidCfg(Dir.Cfg());
        CHECK(ReadJson(Dir.Cfg())["Port"] == 4464);
        CHECK(ReadJson(Dir.Cfg())["Install"] == ValidInstall);
    }
}

TEST_CASE("MR::LoadOrCreateIdentity: conserva un Install alfanumerico valido que no es hex") {
    TempDir Dir;
    const std::string Key = "AbC123xyz" + std::string(23, 'Q'); // 32 caracteres
    WriteAll(Dir.Cfg(), R"({"Install":")" + Key + R"("})");
    CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Install == Key);
}

TEST_CASE("MR::LoadOrCreateIdentity: conserva claves ajenas y valores validos") {
    TempDir Dir;
    WriteAll(Dir.Cfg(),
        R"({"Port":4500,"Build":"EA","CachingDirectory":"./X","Dev":true,"DeleteDuplicateMods":true,)"
        R"("Extra":{"a":[1,2]},"Name":"Piloto_1","Install":")" + ValidInstall + R"(",)"
        R"("Servers":[{"name":"s","ip":"1.2.3.4","port":30814}]})");
    MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(Id.Name == "Piloto_1");
    CHECK(Id.Install == ValidInstall);
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Port"] == 4500);
    CHECK(D["Build"] == "EA");
    CHECK(D["CachingDirectory"] == "./X");
    CHECK(D["Dev"] == true);
    CHECK(D["DeleteDuplicateMods"] == true);
    CHECK(D["Extra"]["a"].size() == 2);
    CHECK(D["Servers"][0]["ip"] == "1.2.3.4");
}

TEST_CASE("MR::LoadOrCreateIdentity: un cfg upstream sin claves MR se completa sin perder nada") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Port":4444,"Build":"Default","CachingDirectory":"./Resources","Dev":false})");
    MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(IsHex48(Id.Install));
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Port"] == 4444); // puerto valido: no se toca
    CHECK(D["CachingDirectory"] == "./Resources");
    CHECK(D["Dev"] == false);
    CHECK(D["Servers"].is_array());
}

TEST_CASE("MR::SetPilotName: nombre valido persiste") {
    TempDir Dir;
    MR::LauncherIdentity Before = MR::LoadOrCreateIdentity(Dir.Cfg());
    std::string Reason = "x";
    CHECK(MR::SetPilotName(Dir.Cfg(), "Piloto_1", Reason));
    CHECK(Reason.empty());
    MR::LauncherIdentity After = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(After.Name == "Piloto_1");
    CHECK(After.Install == Before.Install);
    CheckValidCfg(Dir.Cfg());
}

TEST_CASE("MR::SetPilotName: nombre invalido no toca el archivo") {
    TempDir Dir;
    MR::LoadOrCreateIdentity(Dir.Cfg());
    std::string Content = ReadAll(Dir.Cfg());
    std::string Reason;
    CHECK_FALSE(MR::SetPilotName(Dir.Cfg(), "\xC3\xB1" "and\xC3\xBA", Reason)); // nandu con enie y acento
    CHECK(Reason == NameReason);
    CHECK(ReadAll(Dir.Cfg()) == Content);
}

TEST_CASE("MR::SetPilotName: sobre un archivo inexistente lo crea completo, y conserva claves ajenas") {
    TempDir Dir;
    std::string Reason;
    REQUIRE(MR::SetPilotName(Dir.Cfg(), "Nuevo.Piloto", Reason));
    CheckValidCfg(Dir.Cfg());
    CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Name == "Nuevo.Piloto");

    TempDir Other;
    WriteAll(Other.Cfg(), R"({"Dev":true,"Extra":1,"Install":")" + ValidInstall + R"("})");
    REQUIRE(MR::SetPilotName(Other.Cfg(), "abc", Reason));
    nlohmann::json D = ReadJson(Other.Cfg());
    CHECK(D["Name"] == "abc");
    CHECK(D["Install"] == ValidInstall);
    CHECK(D["Dev"] == true);
    CHECK(D["Extra"] == 1);
}

TEST_CASE("MR::IdentityPacket") {
    MR::LauncherIdentity Id { "Piloto_1", std::string(48, 'a') };
    std::string Packet = MR::IdentityPacket(Id);
    CHECK(Packet == R"({"name":"Piloto_1","install":")" + std::string(48, 'a') + R"("})");
    CHECK(Packet.size() <= 256);

    MR::LauncherIdentity Max { std::string(20, 'z'), std::string(64, 'Z') };
    CHECK(MR::IdentityPacket(Max).size() <= 256);
}

TEST_CASE("RNG caido: nunca se inventa una clave") {
    auto Failing = []() -> std::string { throw std::runtime_error("rng"); };
    auto Weak = []() -> std::string { return "corta"; }; // generador defectuoso: tampoco se acepta

    SUBCASE("archivo inexistente: Install vacio y no se escribe nada") {
        TempDir Dir;
        MR::LauncherIdentity Id;
        REQUIRE_NOTHROW(Id = MR::detail::LoadOrCreateIdentityWith(Dir.Cfg(), Failing));
        CHECK(Id.Install.empty());
        CHECK_FALSE(fs::exists(Dir.Cfg()));
        CHECK_FALSE(fs::exists(Dir.Tmp()));
    }
    SUBCASE("generador defectuoso se trata como fallo") {
        TempDir Dir;
        MR::LauncherIdentity Id = MR::detail::LoadOrCreateIdentityWith(Dir.Cfg(), Weak);
        CHECK(Id.Install.empty());
        CHECK_FALSE(fs::exists(Dir.Cfg()));
    }
    SUBCASE("con Install valido el generador ni se usa") {
        TempDir Dir;
        WriteAll(Dir.Cfg(), R"({"Install":")" + ValidInstall + R"("})");
        MR::LauncherIdentity Id = MR::detail::LoadOrCreateIdentityWith(Dir.Cfg(), Failing);
        CHECK(Id.Install == ValidInstall);
    }
    SUBCASE("archivo corrupto y RNG caido: no se reescribe el archivo") {
        TempDir Dir;
        WriteAll(Dir.Cfg(), "{");
        MR::LauncherIdentity Id = MR::detail::LoadOrCreateIdentityWith(Dir.Cfg(), Failing);
        CHECK(Id.Install.empty());
        CHECK(ReadAll(Dir.Cfg()) == "{");
    }
}
