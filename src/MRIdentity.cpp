/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRIdentity.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace MR {

namespace {

    // ordered_json conserva el orden de las claves al reescribir el archivo.
    using Json = nlohmann::ordered_json;

    constexpr std::size_t NameMin = 3;
    constexpr std::size_t NameMax = 20;
    constexpr std::size_t InstallMin = 32;
    constexpr std::size_t InstallMax = 64;
    constexpr std::size_t GeneratedKeyBytes = 24; // 48 caracteres hex
    constexpr std::uintmax_t MaxConfigBytes = 1024 * 1024;
    constexpr const char* NameReason = "nombre invalido (3-20 caracteres: letras, numeros, _ . -)";
    constexpr const char* SaveReason = "no se pudo guardar la configuracion";

    // Solo ASCII y sin funciones dependientes de la configuracion regional:
    // mismo comportamiento que el server.
    bool IsAlnum(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    bool IsValidPort(const Json& Value) {
        if (!Value.is_number_integer()) {
            return false;
        }
        const auto Port = Value.get<std::int64_t>();
        return Port >= 1 && Port <= 65535;
    }

    // Devuelve el contenido del cfg si es un objeto JSON; en cualquier otro caso
    // (no existe, vacio, ilegible, binario, demasiado grande, no es objeto) un
    // objeto vacio, que la reparacion completa con los valores por defecto.
    Json ReadConfig(const fs::path& File) {
        std::error_code Ec;
        const auto Size = fs::file_size(File, Ec);
        if (Ec || Size == 0 || Size > MaxConfigBytes) {
            return Json::object();
        }
        std::ifstream In(File, std::ios::binary);
        if (!In) {
            return Json::object();
        }
        std::string Buffer(static_cast<std::size_t>(Size), '\0');
        In.read(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
        Buffer.resize(static_cast<std::size_t>(In.gcount()));
        Json Doc = Json::parse(Buffer, nullptr, false);
        if (Doc.is_discarded() || !Doc.is_object()) {
            return Json::object();
        }
        return Doc;
    }

    // Escritura atomica: archivo temporal junto al destino + renombrado. El
    // renombrado reemplaza el destino (en Windows, MoveFileEx con REPLACE_EXISTING
    // dentro de la STL), asi que un corte deja el cfg viejo o el nuevo, nunca uno a medias.
    bool WriteConfigAtomic(const fs::path& File, const Json& Doc) {
        try {
            const fs::path Parent = File.parent_path();
            std::error_code Ec;
            if (!Parent.empty()) {
                fs::create_directories(Parent, Ec);
            }
            fs::path Tmp = File;
            Tmp += ".tmp";
            {
                std::ofstream Out(Tmp, std::ios::binary | std::ios::trunc);
                if (!Out) {
                    return false;
                }
                Out << Doc.dump(4, ' ', false, Json::error_handler_t::replace) << '\n';
                Out.flush();
                if (!Out) {
                    Out.close();
                    fs::remove(Tmp, Ec);
                    return false;
                }
            }
            fs::rename(Tmp, File, Ec);
            if (Ec) {
                fs::remove(Tmp, Ec);
                return false;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    // Completa y corrige las claves que posee el launcher; el resto no se toca.
    // Devuelve true si Doc cambio. Puede lanzar solo por el generador de claves,
    // y la clave se genera al final: si falla, Doc queda sin Install valido.
    bool RepairConfig(Json& Doc, const detail::KeyGenerator& Generate) {
        bool Changed = false;

        auto Port = Doc.find("Port");
        if (Port == Doc.end() || !IsValidPort(*Port)) {
            Doc["Port"] = DefaultPort;
            Changed = true;
        }
        auto Build = Doc.find("Build");
        if (Build == Doc.end() || !Build->is_string()) {
            Doc["Build"] = "Default";
            Changed = true;
        }
        if (!Doc.contains("CachingDirectory")) {
            Doc["CachingDirectory"] = "./Resources";
            Changed = true;
        }
        auto Name = Doc.find("Name");
        if (Name == Doc.end() || !Name->is_string() || (!Name->get_ref<const std::string&>().empty() && !IsValidPilotName(Name->get_ref<const std::string&>()))) {
            Doc["Name"] = "";
            Changed = true;
        }
        auto Servers = Doc.find("Servers");
        if (Servers == Doc.end() || !Servers->is_array()) {
            Doc["Servers"] = Json::array();
            Changed = true;
        }
        auto Install = Doc.find("Install");
        if (Install == Doc.end() || !Install->is_string() || !IsValidInstallKey(Install->get_ref<const std::string&>())) {
            std::string Key = Generate();
            if (!IsValidInstallKey(Key)) {
                throw std::runtime_error("clave de instalacion invalida");
            }
            Doc["Install"] = std::move(Key);
            Changed = true;
        }
        return Changed;
    }

    // Lo que hay en Doc, descartando lo invalido (Install vacio si no hay clave valida).
    LauncherIdentity ExtractIdentity(const Json& Doc) {
        LauncherIdentity Id;
        auto Name = Doc.find("Name");
        if (Name != Doc.end() && Name->is_string() && IsValidPilotName(Name->get_ref<const std::string&>())) {
            Id.Name = Name->get<std::string>();
        }
        auto Install = Doc.find("Install");
        if (Install != Doc.end() && Install->is_string() && IsValidInstallKey(Install->get_ref<const std::string&>())) {
            Id.Install = Install->get<std::string>();
        }
        return Id;
    }

}

bool IsValidPilotName(std::string_view Name) {
    if (Name.size() < NameMin || Name.size() > NameMax) {
        return false;
    }
    for (char c : Name) {
        if (!(IsAlnum(c) || c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool IsValidInstallKey(std::string_view Install) {
    if (Install.size() < InstallMin || Install.size() > InstallMax) {
        return false;
    }
    for (char c : Install) {
        if (!IsAlnum(c)) {
            return false;
        }
    }
    return true;
}

std::string GenerateInstallKey() {
    unsigned char Bytes[GeneratedKeyBytes];
    if (RAND_bytes(Bytes, static_cast<int>(sizeof(Bytes))) != 1) {
        throw std::runtime_error("no se pudo obtener aleatoriedad criptografica");
    }
    static constexpr char Hex[] = "0123456789abcdef";
    std::string Key;
    Key.reserve(GeneratedKeyBytes * 2);
    for (unsigned char B : Bytes) {
        Key += Hex[B >> 4];
        Key += Hex[B & 0x0F];
    }
    return Key;
}

namespace detail {

    LauncherIdentity LoadOrCreateIdentityWith(const fs::path& CfgFile, const KeyGenerator& Generate) {
        try {
            Json Doc = ReadConfig(CfgFile);
            bool Changed = false;
            try {
                Changed = RepairConfig(Doc, Generate);
            } catch (...) {
                // Sin clave valida: no se escribe nada y el llamador ve Install vacio.
                return ExtractIdentity(Doc);
            }
            if (Changed) {
                // Si no se puede guardar, el llamador lo detecta porque el archivo no existe
                // o no cambia; aqui no hay logger al que avisar.
                WriteConfigAtomic(CfgFile, Doc);
            }
            return ExtractIdentity(Doc);
        } catch (...) {
            return {};
        }
    }

}

LauncherIdentity LoadOrCreateIdentity(const fs::path& CfgFile) {
    return detail::LoadOrCreateIdentityWith(CfgFile, GenerateInstallKey);
}

bool SetPilotName(const fs::path& CfgFile, std::string_view Name, std::string& Reason) {
    if (!IsValidPilotName(Name)) {
        Reason = NameReason;
        return false;
    }
    try {
        Json Doc = ReadConfig(CfgFile);
        RepairConfig(Doc, GenerateInstallKey);
        Doc["Name"] = std::string(Name);
        if (!WriteConfigAtomic(CfgFile, Doc)) {
            Reason = SaveReason;
            return false;
        }
        Reason.clear();
        return true;
    } catch (...) {
        Reason = SaveReason;
        return false;
    }
}

std::string IdentityPacket(const LauncherIdentity& Identity) {
    Json Packet = Json::object();
    Packet["name"] = Identity.Name;
    Packet["install"] = Identity.Install;
    return Packet.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}
