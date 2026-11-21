/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Rutas del launcher. Todo lo que escribe (cfg, log, cache de mods) vive en la
// carpeta del ejecutable, nunca en el directorio de trabajo: arrancar el exe por
// su nombre o desde otra carpeta no cambia nada. Sin conversiones a cadena
// estrecha (fallan con rutas fuera de la pagina de codigos ANSI).

#include <filesystem>
#include <string>

namespace MR {

// Valor por defecto de la clave CachingDirectory del cfg (relativa a la carpeta del exe).
constexpr const char* DefaultCacheDir = "Resources";

// Carpeta del ejecutable en ejecucion (GetModuleFileNameW en Windows). Unica fuente
// para el cfg, el log y la cache. Nunca lanza; devuelve una ruta vacia si no puede
// determinarla.
std::filesystem::path ExeDir();

// Carpeta de la cache de mods. Configured es el texto de CachingDirectory: una
// ruta absoluta se respeta tal cual; una relativa (o vacia) cuelga de ExeDir. Una
// ruta con raiz pero no absoluta ("/x", "C:x") tambien cuelga de ExeDir.
std::filesystem::path ResolveCacheDir(const std::filesystem::path& ExeDir, const std::string& Configured);

}
