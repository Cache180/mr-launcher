/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRChannel.h"
#include "MRIdentity.h"
#include "test_support.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using mrtest::ReadAll;
using mrtest::TempDir;
using mrtest::WriteAll;

namespace {

const std::string ValidInstall(48, 'b');

nlohmann::json ReadJson(const fs::path& File) {
    return nlohmann::json::parse(ReadAll(File), nullptr, false);
}

constexpr const char* RejectedPrefix = R"(N{"success":false,"message":")";

} // namespace

TEST_CASE("MR::IsAllowedLink: acepta los tres origenes permitidos") {
    CHECK(MR::IsAllowedLink("https://muertazosracing.com"));
    CHECK(MR::IsAllowedLink("https://muertazosracing.com/"));
    CHECK(MR::IsAllowedLink("https://muertazosracing.com/launcher"));
    CHECK(MR::IsAllowedLink("https://muertazosracing.com?x=1"));
    CHECK(MR::IsAllowedLink("https://muertazosracing.com#top"));
    CHECK(MR::IsAllowedLink("https://discord.gg/abc123"));
    CHECK(MR::IsAllowedLink("https://github.com/Cache180/mr-launcher"));
    CHECK(MR::IsAllowedLink("https://github.com/Cache180/"));
}

TEST_CASE("MR::IsAllowedLink: el host queda anclado (nada de dominios que solo empiezan igual)") {
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com.evil.tld"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com.evil.tld/"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.comx"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com@evil.tld"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com:443/"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com\\@evil.tld"));
    CHECK_FALSE(MR::IsAllowedLink("https://discord.gg"));
    CHECK_FALSE(MR::IsAllowedLink("https://discord.gg.evil.tld/x"));
    CHECK_FALSE(MR::IsAllowedLink("https://discord.gg@evil.tld/"));
    CHECK_FALSE(MR::IsAllowedLink("https://discord.ggx/abc"));
    CHECK_FALSE(MR::IsAllowedLink("https://github.com/Cache180"));
    CHECK_FALSE(MR::IsAllowedLink("https://github.com/Cache1800/x"));
    CHECK_FALSE(MR::IsAllowedLink("https://github.com/Cache180x/x"));
    CHECK_FALSE(MR::IsAllowedLink("https://github.com.evil.tld/Cache180/x"));
}

TEST_CASE("MR::IsAllowedLink: rechaza otros esquemas, otros sitios y formas raras") {
    CHECK_FALSE(MR::IsAllowedLink(""));
    CHECK_FALSE(MR::IsAllowedLink("http://muertazosracing.com"));
    CHECK_FALSE(MR::IsAllowedLink("HTTPS://muertazosracing.com"));
    CHECK_FALSE(MR::IsAllowedLink(" https://muertazosracing.com"));
    CHECK_FALSE(MR::IsAllowedLink("javascript:alert(1)"));
    CHECK_FALSE(MR::IsAllowedLink("file:///C:/Windows/System32/calc.exe"));
    CHECK_FALSE(MR::IsAllowedLink("https://evil.tld/?https://muertazosracing.com"));
    CHECK_FALSE(MR::IsAllowedLink("https://beammp.com"));
    CHECK_FALSE(MR::IsAllowedLink("https://github.com/BeamMP/BeamMP-Launcher"));
    CHECK_FALSE(MR::IsAllowedLink("https://github.com/Cache180/../BeamMP/x"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com/a b"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com/a\nb"));
    CHECK_FALSE(MR::IsAllowedLink(std::string("https://muertazosracing.com/a") + '\0' + "b"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com/a\\b"));
    CHECK_FALSE(MR::IsAllowedLink("https://muertazosracing.com/" + std::string(2100, 'a')));
}

TEST_CASE("MR::HandleIdentityMessage: Nc sin nombre responde Auth 0") {
    TempDir Dir;
    std::string Active;
    CHECK(MR::HandleIdentityMessage("Nc", Dir.Cfg(), Active) == R"(N{"Auth":0})");
}

TEST_CASE("MR::HandleIdentityMessage: Nc con nombre responde Auth 1 con el nombre") {
    TempDir Dir;
    std::string Active = "Piloto_1";
    CHECK(MR::HandleIdentityMessage("Nc", Dir.Cfg(), Active) == R"(N{"Auth":1,"username":"Piloto_1","role":"USER","id":0})");
}

TEST_CASE("MR::HandleIdentityMessage: N:{name} valido guarda el nombre y responde Listo") {
    TempDir Dir;
    std::string Active;
    const std::string Reply = MR::HandleIdentityMessage(R"(N:{"name":"Piloto_1"})", Dir.Cfg(), Active);
    CHECK(Reply == R"(N{"success":true,"message":"Listo","username":"Piloto_1","role":"USER","id":0})");
    CHECK(Active == "Piloto_1");
    CHECK(ReadJson(Dir.Cfg())["Name"] == "Piloto_1");
    CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Name == "Piloto_1");
    // y a partir de ahi Nc lo reporta
    CHECK(MR::HandleIdentityMessage("Nc", Dir.Cfg(), Active) == R"(N{"Auth":1,"username":"Piloto_1","role":"USER","id":0})");
}

TEST_CASE("MR::HandleIdentityMessage: N:{name} acepta claves de mas y espacios en el JSON") {
    TempDir Dir;
    std::string Active;
    const std::string Reply = MR::HandleIdentityMessage(R"(N:{ "password":"x", "name" : "Piloto.9" })", Dir.Cfg(), Active);
    CHECK(Reply == R"(N{"success":true,"message":"Listo","username":"Piloto.9","role":"USER","id":0})");
    CHECK(Active == "Piloto.9");
}

TEST_CASE("MR::HandleIdentityMessage: nombre invalido responde success false y no cambia nada") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Install":")" + ValidInstall + R"(","Name":"Antes"})");
    const std::string Before = ReadAll(Dir.Cfg());
    for (const char* Bad : { "ab", "con espacio", "ni\xC3\xB1o", "a\\nb", "aaaaaaaaaaaaaaaaaaaaa", "" }) {
        CAPTURE(Bad);
        std::string Active = "Antes";
        nlohmann::json Request = { { "name", Bad } };
        const std::string Reply = MR::HandleIdentityMessage("N:" + Request.dump(), Dir.Cfg(), Active);
        CHECK(Reply.rfind(RejectedPrefix, 0) == 0);
        CHECK(Reply.find("nombre invalido") != std::string::npos);
        CHECK(Active == "Antes");
        CHECK(ReadAll(Dir.Cfg()) == Before);
    }
}

TEST_CASE("MR::HandleIdentityMessage: peticiones mal formadas se rechazan sin lanzar ni tocar el cfg") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Install":")" + ValidInstall + R"(","Name":"Antes"})");
    const std::string Before = ReadAll(Dir.Cfg());
    const std::string Huge = R"(N:{"name":")" + std::string(100000, 'a') + R"("})";
    for (const std::string& Bad : { std::string("N:"), std::string("N:basura"), std::string("N:{"), std::string("N:[]"),
             std::string("N:42"), std::string("N:null"), std::string(R"(N:{"name":5})"), std::string(R"(N:{"name":null})"),
             std::string(R"(N:{"name":["abc"]})"), std::string(R"(N:{"nombre":"abc"})"), std::string("N:lo"), Huge }) {
        CAPTURE(Bad.substr(0, 40));
        std::string Active = "Antes";
        std::string Reply;
        REQUIRE_NOTHROW(Reply = MR::HandleIdentityMessage(Bad, Dir.Cfg(), Active));
        CHECK(Reply.rfind(RejectedPrefix, 0) == 0);
        CHECK(Active == "Antes");
        CHECK(ReadAll(Dir.Cfg()) == Before);
    }
}

TEST_CASE("MR::HandleIdentityMessage: N:LO borra el nombre en memoria y en disco y responde vacio") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Install":")" + ValidInstall + R"(","Name":"Piloto_1"})");
    std::string Active = "Piloto_1";
    CHECK(MR::HandleIdentityMessage("N:LO", Dir.Cfg(), Active) == "N");
    CHECK(Active.empty());
    CHECK(MR::LoadOrCreateIdentity(Dir.Cfg()).Name.empty());
    CHECK(ReadJson(Dir.Cfg())["Install"] == ValidInstall);
    CHECK(MR::HandleIdentityMessage("Nc", Dir.Cfg(), Active) == R"(N{"Auth":0})");
}

TEST_CASE("MR::HandleIdentityMessage: si N:LO no puede guardar, lo dice en vez de callar") {
    TempDir Dir;
    WriteAll(Dir.Cfg(), R"({"Install":")" + ValidInstall + R"(","Name":"Piloto_1"})");
    fs::create_directory(Dir.Tmp()); // impide la escritura atomica
    std::string Active = "Piloto_1";
    const std::string Reply = MR::HandleIdentityMessage("N:LO", Dir.Cfg(), Active);
    CHECK(Reply.rfind(RejectedPrefix, 0) == 0);
    CHECK(Active.empty()); // la sesion queda sin nombre aunque el disco no se haya podido actualizar
}

TEST_CASE("MR::HandleIdentityMessage: mensajes que no son del canal de identidad no responden") {
    TempDir Dir;
    std::string Active = "Piloto_1";
    for (const char* Other : { "", "N", "Nx", "Ac", "B", "N;" }) {
        CAPTURE(Other);
        CHECK(MR::HandleIdentityMessage(Other, Dir.Cfg(), Active).empty());
    }
    CHECK(Active == "Piloto_1");
}

TEST_CASE("MR::IsAllowedLink: segmentos punto y separadores codificados con porcentaje se rechazan") {
    for (const char* Base : { "https://github.com/Cache180/", "https://muertazosracing.com/", "https://discord.gg/" }) {
        for (const char* Tail : { "%2e%2e/evil/repo", ".%2e/evil", "%2E./evil", "%2E%2E%2Fevil", "%2e%2e%5cevil",
                 "%2e%2e%5Cevil", "a%2fb", "a%2Fb", "a%5cb", "x/%2e%2e" }) {
            const std::string Link = std::string(Base) + Tail;
            CAPTURE(Link);
            CHECK_FALSE(MR::IsAllowedLink(Link));
        }
    }
}

TEST_CASE("MR::IsAllowedLink: un escape de porcentaje inofensivo sigue aceptandose") {
    CHECK(MR::IsAllowedLink("https://muertazosracing.com/launcher?x=%20"));
    CHECK(MR::IsAllowedLink("https://github.com/Cache180/mr-launcher/releases/tag/v1%2E0") == false); // %2E tambien se rechaza
    CHECK(MR::IsAllowedLink("https://github.com/Cache180/mr%20launcher"));
}
