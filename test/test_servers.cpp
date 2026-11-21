/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRServers.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using nlohmann::json;

namespace {

// Respuesta de la consulta `I` del server MR (el JSON de GenerateCall()).
const char* SampleInfo = R"({
    "players":"2","maxplayers":"20","port":"9999","map":"/levels/gridmap_v2/info.json",
    "private":"false","version":"1.0.0","clientversion":"1.0.0","name":"MR Server Carreras",
    "tags":"Racing","guests":"false","modlist":"/Resources/Client/a.zip;","modstotalsize":"1048576",
    "modstotal":"1","playerslist":"PilotoA;PilotoB;","desc":"Carreras de los viernes"
})";

const std::set<std::string> EntryKeys = { "ip", "port", "sname", "sdesc", "map", "players", "maxplayers",
    "playerslist", "modlist", "modstotal", "modstotalsize", "version", "cversion", "tags", "location",
    "owner", "official", "private", "partner" };

json CfgEntry() {
    return json { { "name", "MR Oficial" }, { "ip", "play.example.org" }, { "port", 4464 } };
}

std::set<std::string> KeysOf(const json& Entry) {
    std::set<std::string> Keys;
    for (auto It = Entry.begin(); It != Entry.end(); ++It) {
        Keys.insert(It.key());
    }
    return Keys;
}

// Las claves y valores por defecto de un server que no contesto.
void CheckDefaults(const json& Entry) {
    CHECK(KeysOf(Entry) == EntryKeys);
    CHECK(Entry["ip"] == "play.example.org");
    CHECK(Entry["port"] == "4464");
    CHECK(Entry["sname"] == "MR Oficial");
    CHECK(Entry["players"] == "0");
    CHECK(Entry["maxplayers"] == "0");
    CHECK(Entry["playerslist"] == "");
    CHECK(Entry["modlist"] == "");
    CHECK(Entry["modstotal"] == "0");
    CHECK(Entry["modstotalsize"] == "0");
    CHECK(Entry["sdesc"] == "");
    CHECK(Entry["map"] == "");
    CHECK(Entry["private"] == false);
    CHECK(Entry["official"] == false);
    CHECK(Entry["partner"] == false);
}

json ServerAt(const std::string& Host, int Port) {
    return json { { "ip", Host }, { "port", Port } };
}

} // namespace

TEST_CASE("MR::BuildServerEntry: con la respuesta I completa trae todas las claves") {
    json Entry = MR::BuildServerEntry(CfgEntry(), SampleInfo);
    CHECK(KeysOf(Entry) == EntryKeys);
    CHECK(Entry["ip"] == "play.example.org");
    CHECK(Entry["port"] == "4464"); // el del cfg, no el que diga el server
    CHECK(Entry["sname"] == "MR Server Carreras");
    CHECK(Entry["sdesc"] == "Carreras de los viernes");
    CHECK(Entry["map"] == "/levels/gridmap_v2/info.json");
    CHECK(Entry["players"] == "2");
    CHECK(Entry["maxplayers"] == "20");
    CHECK(Entry["playerslist"] == "PilotoA;PilotoB;");
    CHECK(Entry["modlist"] == "/Resources/Client/a.zip;");
    CHECK(Entry["modstotal"] == "1");
    CHECK(Entry["modstotalsize"] == "1048576");
    CHECK(Entry["version"] == "1.0.0");
    CHECK(Entry["cversion"] == "1.0.0");
    CHECK(Entry["tags"] == "Racing");
    CHECK(Entry["private"] == false);
    CHECK(Entry["official"] == false);
    CHECK(Entry["partner"] == false);
}

TEST_CASE("MR::BuildServerEntry: private llega como booleano real") {
    json Info = json::parse(SampleInfo);
    Info["private"] = "true";
    CHECK(MR::BuildServerEntry(CfgEntry(), Info.dump())["private"] == true);
    Info["private"] = true;
    CHECK(MR::BuildServerEntry(CfgEntry(), Info.dump())["private"] == true);
    Info["private"] = "false";
    CHECK(MR::BuildServerEntry(CfgEntry(), Info.dump())["private"] == false);
}

TEST_CASE("MR::BuildServerEntry: sin respuesta usa los valores por defecto") {
    CheckDefaults(MR::BuildServerEntry(CfgEntry(), ""));
}

TEST_CASE("MR::BuildServerEntry: una respuesta que no es un objeto JSON equivale a vacia") {
    for (const char* Bad : { "basura", "{", "[1,2,3]", "42", "\"texto\"", "null", "{\"name\":\"\xff\xfe\"}" }) {
        CAPTURE(Bad);
        json Entry;
        REQUIRE_NOTHROW(Entry = MR::BuildServerEntry(CfgEntry(), Bad));
        CheckDefaults(Entry);
    }
}

TEST_CASE("MR::BuildServerEntry: campos con tipo inesperado se convierten o se descartan") {
    json Info = { { "name", 12 }, { "players", 3 }, { "maxplayers", json::array() }, { "map", json::object() },
        { "modlist", nullptr }, { "playerslist", true } };
    json Entry = MR::BuildServerEntry(CfgEntry(), Info.dump());
    CHECK(KeysOf(Entry) == EntryKeys);
    CHECK(Entry["sname"] == "12");
    CHECK(Entry["players"] == "3");
    CHECK(Entry["maxplayers"] == "0");
    CHECK(Entry["map"] == "");
    CHECK(Entry["modlist"] == "");
    CHECK(Entry["playerslist"] == "true");
}

TEST_CASE("MR::BuildServerEntry: sin nombre en la respuesta usa el del cfg; sin ninguno, ip:puerto") {
    json Info = json::parse(SampleInfo);
    Info.erase("name");
    CHECK(MR::BuildServerEntry(CfgEntry(), Info.dump())["sname"] == "MR Oficial");
    CHECK(MR::BuildServerEntry(ServerAt("play.example.org", 4464), "")["sname"] == "play.example.org:4464");
}

TEST_CASE("MR::IsValidServerCfg: acepta entradas bien formadas") {
    CHECK(MR::IsValidServerCfg(CfgEntry()));
    CHECK(MR::IsValidServerCfg(ServerAt("1.2.3.4", 1))); // el nombre es opcional
    CHECK(MR::IsValidServerCfg(json { { "name", "x" }, { "ip", "mr-1.play_example.org" }, { "port", 65535 } }));
}

TEST_CASE("MR::IsValidServerCfg: rechaza lo que no es un {name, ip, port} valido") {
    CHECK_FALSE(MR::IsValidServerCfg(json::array()));
    CHECK_FALSE(MR::IsValidServerCfg(json("texto")));
    CHECK_FALSE(MR::IsValidServerCfg(nullptr));
    CHECK_FALSE(MR::IsValidServerCfg(json { { "port", 4464 } })); // sin ip
    CHECK_FALSE(MR::IsValidServerCfg(json { { "ip", "1.2.3.4" } })); // sin puerto
    CHECK_FALSE(MR::IsValidServerCfg(json { { "name", 7 }, { "ip", "1.2.3.4" }, { "port", 4464 } }));
    for (const json& Ip : { json(5), json(""), json("con espacio"), json("a:b"), json("a/b"), json("a\nb"),
             json(std::string(254, 'a')) }) {
        CAPTURE(Ip.dump());
        CHECK_FALSE(MR::IsValidServerCfg(json { { "ip", Ip }, { "port", 4464 } }));
    }
    for (const json& Port : { json(0), json(-1), json(65536), json("4464"), json(4464.5), json(nullptr), json(true) }) {
        CAPTURE(Port.dump());
        CHECK_FALSE(MR::IsValidServerCfg(json { { "ip", "1.2.3.4" }, { "port", Port } }));
    }
}

TEST_CASE("MR::BuildServerListWith: una entrada por server, en orden, con consulta a cada uno") {
    json Servers = json::array({ json { { "name", "Uno" }, { "ip", "uno.example.org" }, { "port", 4464 } },
        json { { "name", "Dos" }, { "ip", "2.2.2.2" }, { "port", 4470 } } });
    std::mutex Lock;
    std::vector<std::pair<std::string, int>> Calls;
    auto Query = [&](const std::string& Host, int Port) -> std::string {
        std::lock_guard Guard(Lock);
        Calls.emplace_back(Host, Port);
        return Host == "uno.example.org" ? SampleInfo : "";
    };
    json List = json::parse(MR::BuildServerListWith(Servers, Query));
    REQUIRE(List.is_array());
    REQUIRE(List.size() == 2);
    CHECK(List[0]["sname"] == "MR Server Carreras");
    CHECK(List[0]["ip"] == "uno.example.org");
    CHECK(List[1]["sname"] == "Dos"); // no contesto: valores por defecto
    CHECK(List[1]["players"] == "0");
    CHECK(List[1]["port"] == "4470");
    std::sort(Calls.begin(), Calls.end());
    REQUIRE(Calls.size() == 2);
    CHECK(Calls[0] == std::make_pair(std::string("2.2.2.2"), 4470));
    CHECK(Calls[1] == std::make_pair(std::string("uno.example.org"), 4464));
}

TEST_CASE("MR::BuildServerListWith: entradas invalidas se omiten sin consultarlas") {
    json Servers = json::array({ ServerAt("ok.example.org", 4464), json("texto"), ServerAt("x y", 4464),
        ServerAt("ok2.example.org", 70000), json(5), ServerAt("ok3.example.org", 4465) });
    std::atomic<int> Count { 0 };
    auto Query = [&](const std::string&, int) {
        ++Count;
        return std::string();
    };
    json List = json::parse(MR::BuildServerListWith(Servers, Query));
    REQUIRE(List.size() == 2);
    CHECK(List[0]["ip"] == "ok.example.org");
    CHECK(List[1]["ip"] == "ok3.example.org");
    CHECK(Count.load() == 2);
}

TEST_CASE("MR::BuildServerListWith: sin servers validos, un arreglo vacio") {
    auto Query = [](const std::string&, int) { return std::string(); };
    CHECK(MR::BuildServerListWith(json::array(), Query) == "[]");
    CHECK(MR::BuildServerListWith(json::object(), Query) == "[]");
    CHECK(MR::BuildServerListWith(nullptr, Query) == "[]");
    CHECK(MR::BuildServerListWith(json("x"), Query) == "[]");
}

TEST_CASE("MR::BuildServerListWith: una consulta que lanza equivale a un server caido") {
    json Servers = json::array({ json { { "name", "Roto" }, { "ip", "1.1.1.1" }, { "port", 4464 } },
        json { { "name", "Bien" }, { "ip", "2.2.2.2" }, { "port", 4464 } } });
    auto Query = [](const std::string& Host, int) -> std::string {
        if (Host == "1.1.1.1") {
            throw std::runtime_error("boom");
        }
        return SampleInfo;
    };
    json List;
    REQUIRE_NOTHROW(List = json::parse(MR::BuildServerListWith(Servers, Query)));
    REQUIRE(List.size() == 2);
    CHECK(List[0]["sname"] == "Roto");
    CHECK(List[0]["players"] == "0");
    CHECK(List[1]["sname"] == "MR Server Carreras");
}

TEST_CASE("MR::BuildServerListWith: las consultas corren en paralelo (un server lento no suma esperas)") {
    json Servers = json::array();
    for (int I = 0; I < 8; ++I) {
        Servers.push_back(ServerAt("h" + std::to_string(I) + ".example.org", 4464));
    }
    auto Query = [](const std::string&, int) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return std::string();
    };
    const auto Start = std::chrono::steady_clock::now();
    json List = json::parse(MR::BuildServerListWith(Servers, Query));
    const auto Elapsed = std::chrono::steady_clock::now() - Start;
    CHECK(List.size() == 8);
    CHECK(Elapsed < std::chrono::milliseconds(1800)); // en serie serian >= 2400 ms
}

TEST_CASE("MR::BuildServerListWith: no consulta mas de MaxServers entradas") {
    json Servers = json::array();
    for (std::size_t I = 0; I < MR::MaxServers + 6; ++I) {
        Servers.push_back(ServerAt("h" + std::to_string(I) + ".example.org", 4464));
    }
    std::atomic<std::size_t> Count { 0 };
    auto Query = [&](const std::string&, int) {
        ++Count;
        return std::string();
    };
    json List = json::parse(MR::BuildServerListWith(Servers, Query));
    CHECK(List.size() == MR::MaxServers);
    CHECK(Count.load() == MR::MaxServers);
}

TEST_CASE("MR::BuildServerListWith: la salida es JSON valido aunque el server mande bytes invalidos") {
    // Una respuesta con UTF-8 invalido no se parsea (equivale a vacia): el resultado sigue siendo JSON.
    json Servers = json::array({ json { { "name", "Bien" }, { "ip", "2.2.2.2" }, { "port", 4464 } } });
    auto Query = [](const std::string&, int) { return std::string("{\"name\":\"a\xff\"}"); };
    json List;
    REQUIRE_NOTHROW(List = json::parse(MR::BuildServerListWith(Servers, Query)));
    CHECK(List[0]["sname"] == "Bien");
}

TEST_CASE("MR::MaxServerFieldChars: 256") {
    CHECK(MR::MaxServerFieldChars == 256);
}

TEST_CASE("MR::BuildServerEntry: nombre, mapa, descripcion y version de 10000 caracteres quedan en 256") {
    const std::string Long(10000, 'x');
    json Info = { { "name", Long }, { "map", Long }, { "desc", Long }, { "version", Long } };
    json Entry = MR::BuildServerEntry(CfgEntry(), Info.dump());
    CHECK(Entry["sname"].get<std::string>() == std::string(256, 'x'));
    CHECK(Entry["map"].get<std::string>() == std::string(256, 'x'));
    CHECK(Entry["sdesc"].get<std::string>() == std::string(256, 'x'));
    CHECK(Entry["version"].get<std::string>() == std::string(256, 'x'));
}

TEST_CASE("MR::BuildServerEntry: un nombre del cfg desmesurado tambien se recorta") {
    json Cfg = CfgEntry();
    Cfg["name"] = std::string(10000, 'y');
    json Entry = MR::BuildServerEntry(Cfg, "");
    CHECK(Entry["sname"].get<std::string>() == std::string(256, 'y'));
}

TEST_CASE("MR::BuildServerEntry: un campo dentro del limite no cambia") {
    json Info = { { "name", std::string(256, 'z') }, { "map", "/levels/gridmap_v2/info.json" } };
    json Entry = MR::BuildServerEntry(CfgEntry(), Info.dump());
    CHECK(Entry["sname"].get<std::string>() == std::string(256, 'z'));
    CHECK(Entry["map"] == "/levels/gridmap_v2/info.json");
}

TEST_CASE("MR::TruncateServerField: no parte una secuencia UTF-8 y cuenta caracteres") {
    // "\xC3\xB1" = n con tilde (2 bytes, 1 caracter)
    std::string Text;
    for (int I = 0; I < 300; ++I) {
        Text += "\xC3\xB1";
    }
    const std::string Cut = MR::TruncateServerField(Text);
    CHECK(Cut.size() == 512);
    CHECK(Cut == Text.substr(0, 512));
    CHECK(MR::TruncateServerField(Text, 3) == "\xC3\xB1\xC3\xB1\xC3\xB1");
    CHECK(MR::TruncateServerField("", 3) == "");
    CHECK(MR::TruncateServerField("abc", 3) == "abc");
    CHECK(MR::TruncateServerField("abcd", 3) == "abc");
}

TEST_CASE("MR::MaxServerListChars: 4096") {
    CHECK(MR::MaxServerListChars == 4096);
}

TEST_CASE("MR::BuildServerEntry: los textos tipo lista se recortan a 4096") {
    const std::string Long(100000, 'm');
    json Info = { { "playerslist", Long }, { "modlist", Long }, { "tags", Long }, { "clientversion", Long } };
    json Entry = MR::BuildServerEntry(CfgEntry(), Info.dump());
    CHECK(Entry["playerslist"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Entry["modlist"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Entry["tags"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Entry["cversion"].get<std::string>() == std::string(4096, 'm'));
}

TEST_CASE("MR::SanitizeServerInfo: name, map, desc y version a 256; el resto intacto") {
    const std::string Long(10000, 'x');
    json Info = { { "name", Long }, { "map", Long }, { "desc", Long }, { "version", Long }, { "players", "2" }, { "private", "false" } };
    const json Out = json::parse(MR::SanitizeServerInfo(Info.dump()));
    CHECK(Out["name"].get<std::string>() == std::string(256, 'x'));
    CHECK(Out["map"].get<std::string>() == std::string(256, 'x'));
    CHECK(Out["desc"].get<std::string>() == std::string(256, 'x'));
    CHECK(Out["version"].get<std::string>() == std::string(256, 'x'));
    CHECK(Out["players"] == "2");
    CHECK(Out["private"] == "false");
}

TEST_CASE("MR::SanitizeServerInfo: playerslist, modlist, tags y clientversion a 4096") {
    const std::string Long(100000, 'm');
    json Info = { { "playerslist", Long }, { "modlist", Long }, { "tags", Long }, { "clientversion", Long }, { "modlist2", "otra" } };
    const json Out = json::parse(MR::SanitizeServerInfo(Info.dump()));
    CHECK(Out["playerslist"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Out["modlist"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Out["tags"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Out["clientversion"].get<std::string>() == std::string(4096, 'm'));
    CHECK(Out["modlist2"] == "otra");
}

TEST_CASE("MR::SanitizeServerInfo: lo que no es un objeto JSON no se reenvia") {
    CHECK(MR::SanitizeServerInfo("").empty());
    CHECK(MR::SanitizeServerInfo("no es json").empty());
    CHECK(MR::SanitizeServerInfo("[1,2,3]").empty());
    CHECK(MR::SanitizeServerInfo("\"texto\"").empty());
    CHECK(MR::SanitizeServerInfo("42").empty());
}

TEST_CASE("MR::SanitizeServerInfo: un campo que no es texto no se toca y el multibyte no se parte") {
    std::string Text;
    for (int I = 0; I < 300; ++I) {
        Text += "\xC3\xB1";
    }
    json Info = { { "name", Text }, { "version", 7 } };
    const json Out = json::parse(MR::SanitizeServerInfo(Info.dump()));
    CHECK(Out["name"].get<std::string>() == Text.substr(0, 512));
    CHECK(Out["version"] == 7);
}
