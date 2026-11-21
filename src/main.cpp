/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Http.h"
#include "Logger.h"
#include "Network/network.hpp"
#include "Security/Init.h"
#include "Startup.h"
#include "Utils.h"
#include <iostream>
#include <thread>
#include "Options.h"

Options options;

[[noreturn]] void flush() {
    while (true) {
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, const char** argv) try {
#if defined(_WIN32)
    system("cls");
#elif defined(__linux__)
    system("clear");
#endif

#ifdef DEBUG
    std::thread th(flush);
    th.detach();
#endif

    InitLog();
    ConfigInit();
    InitOptions(argc, argv, options);
    InitLauncher();

    info("Basado en BeamMP-Launcher (AGPL-3.0). Codigo: https://github.com/Cache180/mr-launcher");
    info("IMPORTANTE: manten esta ventana abierta mientras juegas en MR.");

    try {
        LegitimacyCheck();
    } catch (std::exception& e) {
        error("Failure in LegitimacyCheck: " + std::string(e.what()));
        throw;
    }

    try {
        HTTP::StartProxy();
    } catch (const std::exception& e) {
        error(std::string("No se pudo iniciar el servidor HTTP local: algunas funciones del juego pueden no funcionar. Error: ") + e.what());
    }
    // El puerto local se enlaza antes de tocar el juego: ocupado = fatal, sin PreGame ni InitGame.
    // Otro fallo (no el puerto ocupado) lo reintenta CoreNetwork cada segundo, como siempre.
    CoreListen();
    PreGame(GetGameDir());
    InitGame(GetGameDir());
    CoreNetwork();
} catch (const std::exception& e) {
    error(std::string("Excepcion en main(): ") + e.what());
    info("Cerrando en 5 segundos");
    info("Si esto sigue pasando, escribenos en https://muertazosracing.com");
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
