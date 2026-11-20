/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Logger.h"
#include "Network/network.hpp"
#include "MRIdentity.h"
#include "Options.h"
#include "Utils.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
namespace fs = std::filesystem;

std::string Branch;
std::filesystem::path CachingDirectory = std::filesystem::path("./Resources");
bool deleteDuplicateMods = false;

void ParseConfig(const nlohmann::json& d) {
    if (d.contains("Port") && d["Port"].is_number()) {
        options.port = d["Port"].get<int>();
    }
    // Default -1
    // Release 1
    // EA 2
    // Dev 3
    // Custom 3
    if (d.contains("Build") && d["Build"].is_string()) {
        Branch = d["Build"].get<std::string>();
        for (char& c : Branch)
            c = char(tolower(c));
    }
    if (d.contains("CachingDirectory") && d["CachingDirectory"].is_string()) {
        CachingDirectory = std::filesystem::path(d["CachingDirectory"].get<std::string>());
        info(beammp_wide("Mod caching directory: ") + beammp_fs_string(CachingDirectory.relative_path()));
    }

    if (d.contains("Dev") && d["Dev"].is_boolean()) {
        bool dev = d["Dev"].get<bool>();
        options.verbose = dev;
        options.no_download = dev;
        options.no_launch = dev;
        options.no_update = dev;
    }

    if (d.contains(("DeleteDuplicateMods")) && d["DeleteDuplicateMods"].is_boolean()) {
        deleteDuplicateMods = d["DeleteDuplicateMods"].get<bool>();
    }

}

void ConfigInit() {
    // Un solo escritor: MR::LoadOrCreateIdentity crea o repara MR-Launcher.cfg
    // (Port, Build, Name, Install, Servers...). Aqui solo se lee.
    const fs::path CfgFile("MR-Launcher.cfg");
    const MR::LauncherIdentity Identity = MR::LoadOrCreateIdentity(CfgFile);
    if (Identity.Install.empty()) {
        fatal("No se pudo leer o guardar la identidad en MR-Launcher.cfg. Cierra cualquier programa que este usando ese archivo (antivirus, sincronizacion) y vuelve a abrir el launcher.");
    }
    if (!fs::exists(CfgFile)) {
        fatal("No se pudo escribir MR-Launcher.cfg en disco.");
    }
    std::ifstream cfg(CfgFile, std::ios::binary);
    if (!cfg.is_open()) {
        fatal("No se pudo abrir MR-Launcher.cfg.");
    }
    auto Size = fs::file_size(CfgFile);
    std::string Buffer(Size, 0);
    cfg.read(&Buffer[0], Size);
    cfg.close();
    nlohmann::json d = nlohmann::json::parse(Buffer, nullptr, false);
    if (d.is_discarded() || !d.is_object()) {
        fatal("MR-Launcher.cfg no es un JSON valido.");
    }
    ParseConfig(d);
}
