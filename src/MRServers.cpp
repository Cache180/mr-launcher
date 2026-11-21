/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRServers.h"

#include <cstdint>
#include <future>
#include <vector>

namespace MR {

namespace {

    using Json = nlohmann::json;

    constexpr std::size_t HostMax = 253;

    // Solo ASCII, sin funciones dependientes de la configuracion regional.
    bool IsHostChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    }

    bool IsValidHost(const Json& Value) {
        if (!Value.is_string()) {
            return false;
        }
        const auto& Host = Value.get_ref<const std::string&>();
        if (Host.empty() || Host.size() > HostMax) {
            return false;
        }
        for (char c : Host) {
            if (!IsHostChar(c)) {
                return false;
            }
        }
        return true;
    }

    bool IsValidPort(const Json& Value) {
        if (!Value.is_number_integer()) {
            return false;
        }
        const auto Port = Value.get<std::int64_t>();
        return Port >= 1 && Port <= 65535;
    }

    // Texto de un campo: cadenas tal cual, numeros y booleanos como texto, el
    // resto (nulo, arreglo, objeto o ausente) es Default.
    std::string TextField(const Json& Object, const char* Key, const std::string& Default) {
        if (!Object.is_object()) {
            return Default;
        }
        const auto It = Object.find(Key);
        if (It == Object.end()) {
            return Default;
        }
        if (It->is_string()) {
            return It->get<std::string>();
        }
        if (It->is_number() || It->is_boolean()) {
            return It->dump();
        }
        return Default;
    }

    bool BoolField(const Json& Object, const char* Key) {
        const auto It = Object.find(Key);
        if (It == Object.end()) {
            return false;
        }
        if (It->is_boolean()) {
            return It->get<bool>();
        }
        return It->is_string() && It->get<std::string>() == "true";
    }

}

std::string TruncateServerField(const std::string& Text, std::size_t Max) {
    std::size_t Chars = 0;
    for (std::size_t I = 0; I < Text.size(); ++I) {
        // Los bytes de continuacion UTF-8 (10xxxxxx) no abren un caracter nuevo.
        if ((static_cast<unsigned char>(Text[I]) & 0xC0) != 0x80 && Chars++ == Max) {
            return Text.substr(0, I);
        }
    }
    return Text;
}

bool IsValidServerCfg(const Json& Cfg) {
    if (!Cfg.is_object()) {
        return false;
    }
    const auto Ip = Cfg.find("ip");
    const auto Port = Cfg.find("port");
    if (Ip == Cfg.end() || !IsValidHost(*Ip) || Port == Cfg.end() || !IsValidPort(*Port)) {
        return false;
    }
    const auto Name = Cfg.find("name");
    return Name == Cfg.end() || Name->is_string();
}

Json BuildServerEntry(const Json& Cfg, const std::string& InfoJson) {
    Json Info = Json::parse(InfoJson, nullptr, false);
    if (!Info.is_object()) {
        Info = Json::object();
    }

    const std::string Ip = TextField(Cfg, "ip", "");
    const std::string Port = TextField(Cfg, "port", "");
    std::string Name = TextField(Info, "name", "");
    if (Name.empty()) {
        Name = TextField(Cfg, "name", "");
    }
    if (Name.empty()) {
        Name = Ip + ":" + Port;
    }

    Json Entry = Json::object();
    Entry["ip"] = Ip;
    Entry["port"] = Port; // el del cfg, no el que diga el server
    Entry["sname"] = TruncateServerField(Name);
    Entry["sdesc"] = TruncateServerField(TextField(Info, "desc", ""));
    Entry["map"] = TruncateServerField(TextField(Info, "map", ""));
    Entry["players"] = TextField(Info, "players", "0");
    Entry["maxplayers"] = TextField(Info, "maxplayers", "0");
    Entry["playerslist"] = TextField(Info, "playerslist", "");
    Entry["modlist"] = TextField(Info, "modlist", "");
    Entry["modstotal"] = TextField(Info, "modstotal", "0");
    Entry["modstotalsize"] = TextField(Info, "modstotalsize", "0");
    Entry["version"] = TruncateServerField(TextField(Info, "version", ""));
    Entry["cversion"] = TextField(Info, "clientversion", "");
    Entry["tags"] = TextField(Info, "tags", "");
    Entry["location"] = "";
    Entry["owner"] = "";
    Entry["official"] = false;
    Entry["private"] = BoolField(Info, "private");
    Entry["partner"] = false;
    return Entry;
}

std::string BuildServerListWith(const Json& Servers, const InfoQuery& Query) {
    Json List = Json::array();
    if (Servers.is_array()) {
        std::vector<const Json*> Valid;
        for (const Json& Cfg : Servers) {
            if (Valid.size() >= MaxServers) {
                break;
            }
            if (IsValidServerCfg(Cfg)) {
                Valid.push_back(&Cfg);
            }
        }

        std::vector<std::future<std::string>> Infos;
        Infos.reserve(Valid.size());
        for (const Json* Cfg : Valid) {
            Infos.push_back(std::async(std::launch::async, [&Query, Cfg]() -> std::string {
                try {
                    return Query(Cfg->at("ip").get<std::string>(), Cfg->at("port").get<int>());
                } catch (...) {
                    return {}; // un server que falla es un server que no contesto
                }
            }));
        }
        for (std::size_t I = 0; I < Valid.size(); ++I) {
            List.push_back(BuildServerEntry(*Valid[I], Infos[I].get()));
        }
    }
    // El texto viene de servers remotos: bytes invalidos se reemplazan, no lanzan.
    return List.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}
