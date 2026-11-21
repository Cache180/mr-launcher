/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Logger.h"
#include "Network/network.hpp"
#include "MRIdentity.h"
#include "MRPaths.h"
#include "MRServers.h"
#include "Options.h"
#include "Startup.h"
#include "Utils.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
namespace fs = std::filesystem;

// Junto al ejecutable (no al directorio de trabajo): se fija en ConfigInit.
fs::path ConfigFile;
std::string Username;
// Credencial: nunca se registra ni se imprime. Solo la lee Auth() para armar la identidad.
std::string InstallKey;
nlohmann::json ConfiguredServers = nlohmann::json::array();
// Carpeta de la cache de mods: se resuelve contra la carpeta del exe en ParseConfig.
std::filesystem::path CachingDirectory;
bool deleteDuplicateMods = false;
// Cfg `Transport: "tcp"`: cada sesion de server arranca ya en el fallback a TCP.
bool ForceTcpTransport = false;

// Servers del cfg: se omiten (con aviso) las entradas que no sean {name, ip, port}
// validas y las que pasen de MR::MaxServers.
static nlohmann::json ParseServers(const nlohmann::json& d) {
    nlohmann::json Valid = nlohmann::json::array();
    if (!d.contains("Servers") || !d["Servers"].is_array()) {
        return Valid;
    }
    std::size_t Index = 0;
    for (const auto& Entry : d["Servers"]) {
        const std::string Where = "Servers[" + std::to_string(Index++) + "] de " + MR::PathToUtf8(ConfigFile);
        if (!MR::IsValidServerCfg(Entry)) {
            warn(Where + " no es un {name, ip, port} valido: se omite.");
        } else if (Valid.size() >= MR::MaxServers) {
            warn(Where + " supera el maximo de " + std::to_string(MR::MaxServers) + " servers: se omite.");
        } else {
            Valid.push_back(Entry);
        }
    }
    return Valid;
}

void ParseConfig(const nlohmann::json& d) {
    if (d.contains("Port") && d["Port"].is_number()) {
        options.port = d["Port"].get<int>();
    }
    // Relativa (o ausente) cuelga de la carpeta del exe, no del directorio de trabajo.
    const std::string Configured = d.contains("CachingDirectory") && d["CachingDirectory"].is_string()
        ? d["CachingDirectory"].get<std::string>()
        : std::string();
    CachingDirectory = MR::ResolveCacheDir(GetBP(), Configured);
    info("Mod caching directory: " + MR::PathToUtf8(CachingDirectory));

    if (d.contains("Dev") && d["Dev"].is_boolean()) {
        bool dev = d["Dev"].get<bool>();
        options.verbose = dev;
        options.no_download = dev;
        options.no_launch = dev;
    }

    if (d.contains(("DeleteDuplicateMods")) && d["DeleteDuplicateMods"].is_boolean()) {
        deleteDuplicateMods = d["DeleteDuplicateMods"].get<bool>();
    }

    ConfiguredServers = ParseServers(d);
}

void ConfigInit() {
    ConfigFile = MR::ConfigPath(GetBP());
    // Un solo escritor: MR::LoadOrCreateIdentity crea o repara MR-Launcher.cfg
    // (Port, Build, Name, Install, Servers...). Aqui solo se lee.
    const MR::LauncherIdentity Identity = MR::LoadOrCreateIdentity(ConfigFile);
    if (Identity.Install.empty()) {
        fatal("No se pudo leer o guardar la identidad en " + MR::PathToUtf8(ConfigFile) + ". Cierra cualquier programa que este usando ese archivo (antivirus, sincronizacion) y vuelve a abrir el launcher.");
    }
    if (!fs::exists(ConfigFile)) {
        fatal("No se pudo escribir " + MR::PathToUtf8(ConfigFile) + " en disco.");
    }
    std::ifstream cfg(ConfigFile, std::ios::binary);
    if (!cfg.is_open()) {
        fatal("No se pudo abrir " + MR::PathToUtf8(ConfigFile) + ".");
    }
    auto Size = fs::file_size(ConfigFile);
    std::string Buffer(Size, 0);
    cfg.read(&Buffer[0], Size);
    cfg.close();
    nlohmann::json d = nlohmann::json::parse(Buffer, nullptr, false);
    if (d.is_discarded() || !d.is_object()) {
        fatal(MR::PathToUtf8(ConfigFile) + " no es un JSON valido.");
    }
    Username = Identity.Name;
    InstallKey = Identity.Install;
    ParseConfig(d);
    // Solo lectura: el valor ya viene reparado ("auto" o "tcp") por MR::LoadOrCreateIdentity.
    ForceTcpTransport = MR::ForceTcpFromConfig(d);
    if (ForceTcpTransport) {
        info("Transporte forzado a TCP por configuracion");
    }
}
