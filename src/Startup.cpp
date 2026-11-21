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

beammp_fs_string GetEN() {
#if defined(_WIN32)
    return L"MR-Launcher.exe";
#elif defined(__linux__)
    return "MR-Launcher";
#endif
}

std::string GetVer() {
    return "1.0";
}
std::string GetPatch() {
    return ".0";
}

beammp_fs_string GetEP(const beammp_fs_char* P) {
    static beammp_fs_string Ret = [&]() {
        beammp_fs_string path(P);
        return path.substr(0, path.find_last_of(beammp_wide("\\/")) + 1);
    }();
    return Ret;
}

fs::path GetBP(const beammp_fs_char* P) {
    fs::path fspath = {};
#if defined(_WIN32)
    // El tamano se pasa en caracteres (no en bytes). 0 es fallo y un resultado igual al
    // tamano es ruta truncada: sin la carpeta del launcher no se puede seguir.
    std::vector<beammp_fs_char> path(32768);
    const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (len == 0 || len >= path.size()) {
        fatal("No se pudo determinar la carpeta del launcher (ruta demasiado larga?)");
    }
    fspath = path.data();
#elif defined(__linux__)
    fspath = fs::canonical("/proc/self/exe");
#elif defined(__APPLE__)
    pid_t pid = getpid();
    char path[PROC_PIDPATHINFO_MAXSIZE];
    // While this is fine for a raw executable,
    // an application bundle is read-only and these files
    // should instead be placed in Application Support.
    proc_pidpath(pid, path, sizeof(path));
    fspath = std::string(path);
#else
    fspath = beammp_fs_string(P);
#endif
    // parent_path (no string + "/.."): sin conversion a cadena estrecha, que lanza con rutas
    // fuera de la pagina de codigos ANSI.
    try {
        fspath = fs::weakly_canonical(fspath.parent_path());
    } catch (const std::exception&) {
        fatal("No se pudo determinar la carpeta del launcher (ruta demasiado larga?)");
    }
#if defined(_WIN32)
    return fspath.wstring();
#else
    return fspath.string();
#endif
}

#if defined(_WIN32)
void ReLaunch() {
    std::wstring Arg;
    for (int c = 2; c <= options.argc; c++) {
        Arg += Utils::ToWString(options.argv[c - 1]);
        Arg += L" ";
    }
    info("Relaunch!");
    system("cls");
    ShellExecuteW(nullptr, L"runas", (GetBP() / GetEN()).c_str(), Arg.c_str(), nullptr, SW_SHOWNORMAL);
    ShowWindow(GetConsoleWindow(), 0);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    exit(1);
}
void URelaunch() {
    std::wstring Arg;
    for (int c = 2; c <= options.argc; c++) {
        Arg += Utils::ToWString(options.argv[c - 1]);
        Arg += L" ";
    }
    ShellExecuteW(nullptr, L"open", (GetBP() / GetEN()).c_str(), Arg.c_str(), nullptr, SW_SHOWNORMAL);
    ShowWindow(GetConsoleWindow(), 0);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    exit(1);
}
#elif defined(__linux__)
void ReLaunch() {
    std::string Arg;
    for (int c = 2; c <= options.argc; c++) {
        Arg += options.argv[c - 1];
        Arg += " ";
    }
    info("Relaunch!");
    system("clear");
    int ret = execv((GetBP() / GetEN()).c_str(), const_cast<char**>(options.argv));
    if (ret < 0) {
        error(std::string("execv() failed with: ") + strerror(errno) + ". Failed to relaunch");
        exit(1);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    exit(1);
}
void URelaunch() {
    int ret = execv((GetBP() / GetEN()).c_str(), const_cast<char**>(options.argv));
    if (ret < 0) {
        error(std::string("execv() failed with: ") + strerror(errno) + ". Failed to relaunch");
        exit(1);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    exit(1);
}
#endif

void CheckName() {
#if defined(_WIN32)
    std::wstring DN = GetEN(), CDir = Utils::ToWString(options.executable_name), FN = CDir.substr(CDir.find_last_of('\\') + 1);
#elif defined(__linux__)
    std::string DN = GetEN(), CDir = options.executable_name, FN = CDir.substr(CDir.find_last_of('/') + 1);
#endif
    if (FN != DN) {
        if (fs::exists(DN))
            fs::remove(DN.c_str());
        if (fs::exists(DN))
            ReLaunch();
        fs::rename(FN.c_str(), DN.c_str());
        URelaunch();
    }
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
    CheckName();
    LinuxPatch();
}
#elif defined(__linux__)

void InitLauncher() {
    info("MR Launcher v" + GetVer() + GetPatch());
    CheckName();
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
