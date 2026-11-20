/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRChannel.h"

#include "MRIdentity.h"

#include <cstddef>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace MR {

namespace {

    // ordered_json conserva el orden de las claves tal como el mod lo espera.
    using Json = nlohmann::ordered_json;

    constexpr std::size_t MaxLinkLength = 2048;
    // Una peticion de nombre legitima mide unas decenas de bytes.
    constexpr std::size_t MaxRequestBytes = 1024;
    constexpr const char* InvalidRequest = "peticion invalida";

    bool StartsWith(std::string_view Text, std::string_view Prefix) {
        return Text.substr(0, Prefix.size()) == Prefix;
    }

    // "N" + JSON.
    std::string Packet(const Json& Body) {
        return "N" + Body.dump(-1, ' ', false, Json::error_handler_t::replace);
    }

    std::string Rejected(const std::string& Reason) {
        Json Body = Json::object();
        Body["success"] = false;
        Body["message"] = Reason;
        return Packet(Body);
    }

    std::string AuthStatus(const std::string& Name) {
        Json Body = Json::object();
        Body["Auth"] = Name.empty() ? 0 : 1;
        if (!Name.empty()) {
            Body["username"] = Name;
            Body["role"] = "USER";
            Body["id"] = 0;
        }
        return Packet(Body);
    }

    std::string ChangeName(std::string_view Payload, const fs::path& CfgFile, std::string& ActiveName) {
        if (Payload.size() > MaxRequestBytes) {
            return Rejected(InvalidRequest);
        }
        const Json Request = Json::parse(std::string(Payload), nullptr, false);
        if (!Request.is_object()) {
            return Rejected(InvalidRequest);
        }
        const auto Name = Request.find("name");
        if (Name == Request.end() || !Name->is_string()) {
            return Rejected(InvalidRequest);
        }
        const std::string Wanted = Name->get<std::string>();
        std::string Reason;
        if (!SetPilotName(CfgFile, Wanted, Reason)) {
            return Rejected(Reason);
        }
        ActiveName = Wanted;
        Json Body = Json::object();
        Body["success"] = true;
        Body["message"] = "Listo";
        Body["username"] = ActiveName;
        Body["role"] = "USER";
        Body["id"] = 0;
        return Packet(Body);
    }

    std::string Logout(const fs::path& CfgFile, std::string& ActiveName) {
        ActiveName.clear();
        std::string Reason;
        if (!ClearPilotName(CfgFile, Reason)) {
            return Rejected(Reason);
        }
        return "N";
    }

}

bool IsAllowedLink(std::string_view Link) {
    if (Link.empty() || Link.size() > MaxLinkLength || Link.find("..") != std::string_view::npos) {
        return false;
    }
    for (char C : Link) {
        const auto Byte = static_cast<unsigned char>(C);
        if (Byte <= 0x20 || Byte == 0x7F || C == '\\') {
            return false;
        }
    }
    constexpr std::string_view Site = "https://muertazosracing.com";
    if (StartsWith(Link, Site)) {
        // El host termina aqui: nada de muertazosracing.com.otro.tld ni muertazosracing.com@otro.tld.
        const std::string_view Rest = Link.substr(Site.size());
        return Rest.empty() || Rest[0] == '/' || Rest[0] == '?' || Rest[0] == '#';
    }
    return StartsWith(Link, "https://discord.gg/") || StartsWith(Link, "https://github.com/Cache180/");
}

std::string HandleIdentityMessage(const std::string& Data, const fs::path& CfgFile, std::string& ActiveName) {
    try {
        if (Data.size() < 2 || Data[0] != 'N') {
            return {};
        }
        if (Data[1] == 'c') {
            return AuthStatus(ActiveName);
        }
        if (Data[1] != ':') {
            return {};
        }
        const std::string_view Payload = std::string_view(Data).substr(2);
        return Payload == "LO" ? Logout(CfgFile, ActiveName) : ChangeName(Payload, CfgFile, ActiveName);
    } catch (...) {
        return R"(N{"success":false,"message":"error interno"})";
    }
}

}
