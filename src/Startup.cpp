/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "zip_file.h"
#include <cstring>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#if defined(_WIN32)
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <unistd.h>
#endif // __APPLE__
#include "MRMods.h"
#include "MRPaths.h"
#include "Logger.h"
#include "Network/network.hpp"
#include "Options.h"
#include "Security/Init.h"
#include "Startup.h"
#include "Utils.h"
#include "hashpp.h"
#include <filesystem>
#include <fstream>
#include <thread>

int ProxyPort = 0;

namespace fs = std::filesystem;

std::string GetVer() {
    return "1.1";
}
std::string GetPatch() {
    return ".0";
}

// Carpeta del exe: MR::ExeDir es la unica fuente (cfg, log y cache cuelgan de ella).
fs::path GetBP() {
    static const fs::path Dir = MR::ExeDir();
    if (Dir.empty()) {
        fatal("No se pudo determinar la carpeta del launcher (ruta demasiado larga?)");
    }
    return Dir;
}

#ifdef _WIN32
void LinuxPatch() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, R"(Software\Wine)", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS || getenv("USER") == nullptr)
        return;
    RegCloseKey(hKey);
    info("Wine/Proton Detected! If you are on windows delete HKEY_CURRENT_USER\\Software\\Wine in regedit");
    info("Applying patches...");

    result = RegCreateKey(HKEY_CURRENT_USER, R"(Software\Valve\Steam\Apps\284160)", &hKey);

    if (result != ERROR_SUCCESS) {
        fatal(R"(failed to create HKEY_CURRENT_USER\Software\Valve\Steam\Apps\284160)");
        return;
    }

    result = RegSetValueEx(hKey, "Name", 0, REG_SZ, (BYTE*)"BeamNG.drive", 12);

    if (result != ERROR_SUCCESS) {
        fatal(R"(failed to create the value "Name" under HKEY_CURRENT_USER\Software\Valve\Steam\Apps\284160)");
        return;
    }
    RegCloseKey(hKey);

    info("Patched!");
}
#endif

#if defined(_WIN32)

void InitLauncher() {
    SetConsoleTitleA(("MR Launcher v" + std::string(GetVer()) + GetPatch()).c_str());
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_U8TEXT);
    debug("Launcher Version : " + GetVer() + GetPatch());
    LinuxPatch();
}
#elif defined(__linux__)

void InitLauncher() {
    info("MR Launcher v" + GetVer() + GetPatch());
}
#endif

void EnableMP() {
    beammp_fs_string File(GetGamePath() / beammp_wide("mods/db.json"));
    if (!fs::exists(File))
        return;
    auto Size = fs::file_size(File);
    if (Size < 2)
        return;
    std::ifstream db(File);
    if (db.is_open()) {
        std::string Data(Size, 0);
        db.read(&Data[0], Size);
        db.close();
        nlohmann::json d = nlohmann::json::parse(Data, nullptr, false);
        if (Data.at(0) != '{' || d.is_discarded()) {
            // error("Failed to parse " + File); //TODO illegal formatting
            return;
        }
        if (d.contains("mods") && d["mods"].contains("multiplayermrmp")) {
            d["mods"]["multiplayermrmp"]["active"] = true;
            std::ofstream ofs(File);
            if (ofs.is_open()) {
                ofs << d.dump();
                ofs.close();
            } else {
                error(beammp_wide("Failed to write ") + File);
            }
        }
    }
}

void PreGame(const beammp_fs_string& GamePath) {
    std::string GameVer = CheckVer(GamePath);
    info("Game Version : " + GameVer);

    try {
        MR::CleanMultiplayerDir(GetGamePath() / beammp_wide("mods/multiplayer"));
    } catch (...) {
        fatal("No se pudo limpiar la carpeta de mods multijugador. Cierra el juego y cualquier programa que tenga abierta esa carpeta.");
    }
    info(beammp_wide("Game user path: ") + beammp_fs_string(GetGamePath()));

    if (!options.no_download) {
        std::string Error;
        if (!MR::InstallBundledMod(GetBP(), GetGamePath(), Error)) {
            fatal(Error);
        }
        try {
            EnableMP();
        } catch (std::exception& e) {
            fatal(e.what());
        }

        beammp_fs_string Target(GetGamePath() / beammp_wide("mods/unpacked/mrmp"));

        if (fs::is_directory(Target) && !fs::is_directory(Target + beammp_wide("/.git"))) {
            fs::remove_all(Target);
        }
    }
}
