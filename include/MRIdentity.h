/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Identidad local del launcher: nombre de piloto autodeclarado + clave de
// instalacion aleatoria. Modulo puro: no depende de los globales ni del logger
// del launcher, para poder enlazarse solo en el binario de pruebas.
//
// La clave de instalacion es una credencial: nunca se registra ni se imprime.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace MR {

constexpr int DefaultPort = 4464;

struct LauncherIdentity {
    std::string Name;
    std::string Install;
};

// 3-20 caracteres, solo [A-Za-z0-9_.-] (ASCII; mismo criterio que el server).
bool IsValidPilotName(std::string_view Name);

// 32-64 caracteres, solo [A-Za-z0-9].
bool IsValidInstallKey(std::string_view Install);

// 48 caracteres hex en minusculas de RAND_bytes (OpenSSL).
// Lanza std::runtime_error si el generador del sistema falla: nunca hay
// alternativa debil.
std::string GenerateInstallKey();

// Lee el cfg, repara lo que falte o sea invalido (conservando las demas claves)
// y lo reescribe de forma atomica solo si hubo cambios. Nunca lanza.
// Si no puede obtener una clave de instalacion (RNG caido) devuelve Install
// vacio y NO escribe nada: el llamador debe tratarlo como error fatal.
LauncherIdentity LoadOrCreateIdentity(const std::filesystem::path& CfgFile);

// Valida y guarda el nombre. Si es invalido deja Reason y no toca el archivo.
bool SetPilotName(const std::filesystem::path& CfgFile, std::string_view Name, std::string& Reason);

// Borra el nombre guardado (queda "" y el resto del cfg se conserva). Si no puede
// guardar, deja Reason y no toca el archivo.
bool ClearPilotName(const std::filesystem::path& CfgFile, std::string& Reason);

// {"name":"...","install":"..."} (JSON UTF-8, <= 256 bytes con valores validos).
std::string IdentityPacket(const LauncherIdentity& Identity);

namespace detail {
    using KeyGenerator = std::function<std::string()>;
    // Igual que LoadOrCreateIdentity pero con generador inyectable (pruebas).
    LauncherIdentity LoadOrCreateIdentityWith(const std::filesystem::path& CfgFile, const KeyGenerator& Generate);
}

}
