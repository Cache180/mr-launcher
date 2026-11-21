/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRIdentity.h"
#include "test_support.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <string>

namespace fs = std::filesystem;

namespace {

using mrtest::ReadAll;
using mrtest::TempDir;
using mrtest::WriteAll;

constexpr const char* NameReason = "nombre invalido (3-20 caracteres: letras, numeros, _ . -)";

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
    CHECK(MR::IsValidPilotName("Piloto.Uno_mr-123")); // 17
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

// ---- Revision 1: lectura transitoria, copia .corrupt y fallos de escritura ----

namespace {

fs::path CorruptOf(const fs::path& Cfg) {
    return fs::path(Cfg.string() + ".corrupt");
}

#ifdef _WIN32
// Mantiene el archivo abierto sin compartir (solo Windows: CreateFileW con
// dwShareMode = 0), lo que reproduce una violacion de uso compartido
// (antivirus, sincronizador) para cualquier otro proceso o handle.
class ExclusiveLock {
public:
    explicit ExclusiveLock(const fs::path& File)
        : mHandle(CreateFileW(File.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) { }
    ~ExclusiveLock() { Release(); }
    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;
    bool Held() const { return mHandle != INVALID_HANDLE_VALUE; }
    void Release() {
        if (mHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(mHandle);
            mHandle = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE mHandle;
};
#endif

} // namespace

#ifdef _WIN32
TEST_CASE("archivo valido bloqueado: no se rota la clave ni se toca el archivo") {
    TempDir Dir;
    const std::string Original = R"({"Port":4464,"Build":"Default","Name":"Piloto_1","Install":")" + ValidInstall + R"("})";
    WriteAll(Dir.Cfg(), Original);

    SUBCASE("LoadOrCreateIdentity") {
        {
            ExclusiveLock Lock(Dir.Cfg());
            REQUIRE(Lock.Held());
            MR::LauncherIdentity Id;
            REQUIRE_NOTHROW(Id = MR::LoadOrCreateIdentity(Dir.Cfg()));
            CHECK(Id.Install.empty());
        }
        CHECK(ReadAll(Dir.Cfg()) == Original);
        CHECK_FALSE(fs::exists(CorruptOf(Dir.Cfg())));
        CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Install == ValidInstall);
    }
    SUBCASE("SetPilotName") {
        {
            ExclusiveLock Lock(Dir.Cfg());
            REQUIRE(Lock.Held());
            std::string Reason;
            CHECK_FALSE(MR::SetPilotName(Dir.Cfg(), "Otro_Nombre", Reason));
            CHECK_FALSE(Reason.empty());
            CHECK(Reason.find(ValidInstall) == std::string::npos);
        }
        CHECK(ReadAll(Dir.Cfg()) == Original);
        CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Name == "Piloto_1");
    }
}
#endif

TEST_CASE("cfg corrupto no vacio: se repara y el contenido viejo queda en .corrupt") {
    static const char Binary[] = "\x00\xFF\xFE\x80\x01\x02" "garbage\xC3\x28";
    const std::string Cases[] = {
        "{",
        "[1,2,3]",
        std::string(Binary, sizeof(Binary) - 1),
        R"({"Name":"Piloto_1","Install":"corta","Extra":1})", // JSON valido pero sin Install valido
        std::string(1024 * 1024 + 1, 'x'),                     // mas de 1 MiB
    };
    for (const std::string& Old : Cases) {
        TempDir Dir;
        WriteAll(Dir.Cfg(), Old);
        MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
        CHECK(IsHex48(Id.Install));
        CheckValidCfg(Dir.Cfg());
        REQUIRE(fs::exists(CorruptOf(Dir.Cfg())));
        CHECK(ReadAll(CorruptOf(Dir.Cfg())) == Old);
    }
}

TEST_CASE("cfg corrupto: una copia .corrupt anterior se sobrescribe") {
    TempDir Dir;
    WriteAll(CorruptOf(Dir.Cfg()), "copia vieja");
    WriteAll(Dir.Cfg(), "{roto");
    MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(ReadAll(CorruptOf(Dir.Cfg())) == "{roto");
}

TEST_CASE("sin copia .corrupt cuando no hay nada que perder") {
    SUBCASE("archivo inexistente") {
        TempDir Dir;
        MR::LoadOrCreateIdentity(Dir.Cfg());
        CHECK_FALSE(fs::exists(CorruptOf(Dir.Cfg())));
    }
    SUBCASE("archivo de longitud cero") {
        TempDir Dir;
        WriteAll(Dir.Cfg(), "");
        CHECK(IsHex48(MR::LoadOrCreateIdentity(Dir.Cfg()).Install));
        CHECK_FALSE(fs::exists(CorruptOf(Dir.Cfg())));
    }
    SUBCASE("Install valido con otras claves invalidas") {
        TempDir Dir;
        WriteAll(Dir.Cfg(), R"({"Port":"texto","Name":"a b","Install":")" + ValidInstall + R"("})");
        CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Install == ValidInstall);
        CHECK_FALSE(fs::exists(CorruptOf(Dir.Cfg())));
    }
}

TEST_CASE("si la copia .corrupt falla no se sobrescribe nada") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), "{");
    fs::create_directory(CorruptOf(Dir.Cfg())); // un directorio ocupa el destino de la copia
    MR::LauncherIdentity Id;
    REQUIRE_NOTHROW(Id = MR::LoadOrCreateIdentity(Dir.Cfg()));
    CHECK(Id.Install.empty());
    CHECK(ReadAll(Dir.Cfg()) == "{");
    std::string Reason;
    CHECK_FALSE(MR::SetPilotName(Dir.Cfg(), "Piloto_1", Reason));
    CHECK(ReadAll(Dir.Cfg()) == "{");
}

TEST_CASE("fallo de escritura: Install vacio, SetPilotName falso, sin restos a medias") {
    SUBCASE("archivo inexistente y un directorio ocupa el temporal") {
        TempDir Dir;
        fs::create_directory(Dir.Tmp());
        MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
        CHECK(Id.Install.empty());
        CHECK_FALSE(fs::exists(Dir.Cfg()));
    }
    SUBCASE("cfg valido pero incompleto y un directorio ocupa el temporal") {
        TempDir Dir;
        const std::string Old = R"({"Install":")" + ValidInstall + R"("})";
        WriteAll(Dir.Cfg(), Old);
        fs::create_directory(Dir.Tmp());
        CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Install.empty()); // nunca una clave que no esta en disco
        CHECK(ReadAll(Dir.Cfg()) == Old);
        std::string Reason;
        CHECK_FALSE(MR::SetPilotName(Dir.Cfg(), "Piloto_1", Reason));
        CHECK_FALSE(Reason.empty());
        CHECK(ReadAll(Dir.Cfg()) == Old);
    }
    SUBCASE("un directorio ocupa la ruta del cfg") {
        TempDir Dir;
        fs::create_directory(Dir.Cfg());
        CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Install.empty());
        std::string Reason;
        CHECK_FALSE(MR::SetPilotName(Dir.Cfg(), "Piloto_1", Reason));
        CHECK(fs::is_directory(Dir.Cfg()));
        CHECK_FALSE(fs::exists(Dir.Tmp()));
    }
}

TEST_CASE("MR::ClearPilotName: borra el nombre y conserva el resto del cfg") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Install":")" + ValidInstall + R"(","Name":"Piloto_1","Port":4470,"Servers":[{"name":"MR","ip":"1.2.3.4","port":4464}]})");
    std::string Reason = "previo";
    REQUIRE(MR::ClearPilotName(Dir.Cfg(), Reason));
    CHECK(Reason.empty());
    CheckValidCfg(Dir.Cfg());
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Name"] == "");
    CHECK(D["Install"] == ValidInstall);
    CHECK(D["Port"] == 4470);
    REQUIRE(D["Servers"].size() == 1);
    CHECK(D["Servers"][0]["ip"] == "1.2.3.4");
    CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Name.empty());
}

TEST_CASE("MR::ClearPilotName: sin cfg previo lo crea valido y sin nombre") {
    TempDir Dir;
    std::string Reason;
    REQUIRE(MR::ClearPilotName(Dir.Cfg(), Reason));
    CheckValidCfg(Dir.Cfg());
    CHECK(ReadJson(Dir.Cfg())["Name"] == "");
}

TEST_CASE("MR::ClearPilotName: fallo de escritura devuelve falso con motivo y no toca el cfg") {
    TempDir Dir;
    const std::string Old = R"({"Install":")" + ValidInstall + R"(","Name":"Piloto_1"})";
    WriteAll(Dir.Cfg(), Old);
    fs::create_directory(Dir.Tmp());
    std::string Reason;
    CHECK_FALSE(MR::ClearPilotName(Dir.Cfg(), Reason));
    CHECK_FALSE(Reason.empty());
    CHECK(ReadAll(Dir.Cfg()) == Old);
}

TEST_CASE("MR::ConfigPath: el cfg queda junto al ejecutable") {
    TempDir Dir;
    const fs::path Cfg = MR::ConfigPath(Dir.Path());
    CHECK(Cfg.filename() == "MR-Launcher.cfg");
    CHECK(Cfg.parent_path() == Dir.Path());
    CHECK(Cfg == Dir.Cfg());
}

TEST_CASE("MR::ConfigPath: no depende del directorio de trabajo") {
    TempDir Exe;
    TempDir Elsewhere;
    const fs::path Before = fs::current_path();
    const fs::path FromHere = MR::ConfigPath(Exe.Path());
    fs::current_path(Elsewhere.Path());
    const fs::path FromThere = MR::ConfigPath(Exe.Path());
    fs::current_path(Before);
    CHECK(FromHere == FromThere);
    CHECK(FromThere.is_absolute());
    CHECK(FromThere.parent_path() == Exe.Path());
}

// --- Clave Transport ("auto" | "tcp") -------------------------------------------

TEST_CASE("Transport: sin la clave se agrega como \"auto\" y Install queda intacto") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Port":4500,"Name":"PilotoA","Install":")" + ValidInstall + R"(","Extra":{"a":1}})");
    MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(Id.Install == ValidInstall);
    CHECK(Id.Name == "PilotoA");
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Transport"] == "auto");
    CHECK(D["Install"] == ValidInstall);
    CHECK(D["Extra"]["a"] == 1);
    CHECK(D["Port"] == 4500);
    CHECK_FALSE(MR::ForceTcpFromConfig(D));
}

TEST_CASE("Transport: un cfg nuevo trae \"auto\"") {
    TempDir Dir;
    MR::LoadOrCreateIdentity(Dir.Cfg());
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Transport"] == "auto");
    CheckValidCfg(Dir.Cfg());
}

TEST_CASE("Transport: \"tcp\" se conserva y fuerza el fallback") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Transport":"tcp","Install":")" + ValidInstall + R"("})");
    MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
    CHECK(Id.Install == ValidInstall);
    nlohmann::json D = ReadJson(Dir.Cfg());
    CHECK(D["Transport"] == "tcp");
    CHECK(D["Install"] == ValidInstall);
    CHECK(MR::ForceTcpFromConfig(D));
    CHECK(MR::ForceTcpFromConfig(nlohmann::ordered_json::parse(R"({"Transport":"tcp"})")));
}

TEST_CASE("Transport: valores invalidos se reparan a \"auto\" sin tocar Install") {
    const char* Invalid[] = { "7", "\"quic\"", "\"TCP\"", "\"\"", "null", "true", "[\"tcp\"]", "{\"a\":1}", "1.5" };
    for (const char* Bad : Invalid) {
        CAPTURE(Bad);
        TempDir Dir;
        WriteAll(Dir.Cfg(), std::string(R"({"Transport":)") + Bad + R"(,"Install":")" + ValidInstall + R"(","Extra":"x"})");
        MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
        CHECK(Id.Install == ValidInstall);
        nlohmann::json D = ReadJson(Dir.Cfg());
        CHECK(D["Transport"] == "auto");
        CHECK(D["Install"] == ValidInstall);
        CHECK(D["Extra"] == "x");
        CHECK_FALSE(MR::ForceTcpFromConfig(D));
    }
}

TEST_CASE("Transport: un cfg completo y valido no se reescribe") {
    for (const char* Mode : { "auto", "tcp" }) {
        CAPTURE(Mode);
        TempDir Dir;
        WriteAll(Dir.Cfg(),
            R"({"Port":4464,"Build":"Default","CachingDirectory":"./Resources","Name":"PilotoA","Servers":[],)"
            R"("Install":")" + ValidInstall + R"(","Transport":")" + Mode + R"("})");
        const std::string Before = ReadAll(Dir.Cfg());
        MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
        CHECK(Id.Install == ValidInstall);
        CHECK(ReadAll(Dir.Cfg()) == Before);
    }
}

TEST_CASE("Transport: SetPilotName y ClearPilotName conservan la clave") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Transport":"tcp","Install":")" + ValidInstall + R"("})");
    std::string Reason;
    REQUIRE(MR::SetPilotName(Dir.Cfg(), "PilotoB", Reason));
    CHECK(ReadJson(Dir.Cfg())["Transport"] == "tcp");
    REQUIRE(MR::ClearPilotName(Dir.Cfg(), Reason));
    CHECK(ReadJson(Dir.Cfg())["Transport"] == "tcp");
    CHECK(ReadJson(Dir.Cfg())["Install"] == ValidInstall);
}

TEST_CASE("MR::ForceTcpFromConfig: solo la cadena \"tcp\" fuerza (json y ordered_json)") {
    using nlohmann::json;
    using nlohmann::ordered_json;
    CHECK(MR::ForceTcpFromConfig(json { { "Transport", "tcp" } }));
    CHECK(MR::ForceTcpFromConfig(ordered_json { { "Transport", "tcp" } }));
    CHECK_FALSE(MR::ForceTcpFromConfig(json::object()));
    CHECK_FALSE(MR::ForceTcpFromConfig(ordered_json::object()));
    for (const char* Other : { "auto", "TCP", "udp", "quic", "" }) {
        CAPTURE(Other);
        CHECK_FALSE(MR::ForceTcpFromConfig(json { { "Transport", Other } }));
        CHECK_FALSE(MR::ForceTcpFromConfig(ordered_json { { "Transport", Other } }));
    }
    CHECK_FALSE(MR::ForceTcpFromConfig(json { { "Transport", 7 } }));
    CHECK_FALSE(MR::ForceTcpFromConfig(ordered_json { { "Transport", true } }));
    CHECK_FALSE(MR::ForceTcpFromConfig(json { { "Transport", nullptr } }));
    CHECK_FALSE(MR::ForceTcpFromConfig(json::array({ "tcp" })));
    CHECK_FALSE(MR::ForceTcpFromConfig(json(nullptr)));
}

namespace {

// Archivos del directorio que terminan en .tmp (restos de una escritura).
std::size_t CountTmpFiles(const fs::path& Dir) {
    std::size_t Count = 0;
    for (const auto& Entry : fs::directory_iterator(Dir)) {
        if (Entry.path().extension() == ".tmp") {
            ++Count;
        }
    }
    return Count;
}

}

TEST_CASE("TempConfigPath: un temporal por proceso, junto al cfg y con el pid") {
    TempDir Dir;
    const fs::path A = MR::detail::TempConfigPath(Dir.Cfg(), 1111);
    const fs::path B = MR::detail::TempConfigPath(Dir.Cfg(), 2222);
    CHECK(A != B);
    CHECK(A.parent_path() == Dir.Path());
    CHECK(A.filename().string() == "MR-Launcher.cfg.1111.tmp");
    CHECK(B.filename().string() == "MR-Launcher.cfg.2222.tmp");
}

TEST_CASE("WriteConfigAtomicTo: dos escritores con temporales distintos no se pisan") {
    TempDir Dir;
    const fs::path TmpA = MR::detail::TempConfigPath(Dir.Cfg(), 1111);
    const fs::path TmpB = MR::detail::TempConfigPath(Dir.Cfg(), 2222);
    // El otro proceso esta a mitad de su escritura: su temporal existe con contenido parcial.
    WriteAll(TmpB, "{\"Parcial\":");
    nlohmann::ordered_json Doc = nlohmann::ordered_json::object();
    Doc["Name"] = "PilotoA";
    REQUIRE(MR::detail::WriteConfigAtomicTo(Dir.Cfg(), Doc, TmpA));
    CHECK(ReadJson(Dir.Cfg())["Name"] == "PilotoA");
    CHECK_FALSE(fs::exists(TmpA));
    CHECK(ReadAll(TmpB) == "{\"Parcial\":"); // intacto
}

TEST_CASE("WriteConfigAtomicTo: un fallo no deja temporal") {
    TempDir Dir;
    // El destino es un directorio: el renombrado falla y el temporal debe borrarse.
    fs::create_directory(Dir.Cfg());
    const fs::path Tmp = MR::detail::TempConfigPath(Dir.Cfg(), 1111);
    nlohmann::ordered_json Doc = nlohmann::ordered_json::object();
    Doc["Name"] = "PilotoA";
    CHECK_FALSE(MR::detail::WriteConfigAtomicTo(Dir.Cfg(), Doc, Tmp));
    CHECK_FALSE(fs::exists(Tmp));
}

TEST_CASE("Escritura del cfg: no queda ningun *.tmp tras crear, reparar y guardar el nombre") {
    TempDir Dir;
    const MR::LauncherIdentity Id = MR::LoadOrCreateIdentity(Dir.Cfg());
    REQUIRE_FALSE(Id.Install.empty());
    CHECK(CountTmpFiles(Dir.Path()) == 0);
    std::string Reason;
    REQUIRE(MR::SetPilotName(Dir.Cfg(), "PilotoB", Reason));
    CHECK(CountTmpFiles(Dir.Path()) == 0);
    REQUIRE(MR::ClearPilotName(Dir.Cfg(), Reason));
    CHECK(CountTmpFiles(Dir.Path()) == 0);
}
