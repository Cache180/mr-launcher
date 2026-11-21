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

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace MR {

constexpr int DefaultPort = 4464;

struct LauncherIdentity {
    std::string Name;
    std::string Install;
};

// Ruta -> UTF-8 para mensajes y registros. A diferencia de path::string() (que con MSVC lanza
// si hay caracteres fuera de la pagina de codigos ANSI) nunca lanza.
std::string PathToUtf8(const std::filesystem::path& Path);

// Nombre del archivo de configuracion. Vive junto al ejecutable, nunca en el
// directorio de trabajo: arrancar el launcher desde otra carpeta no puede crear
// una segunda identidad.
constexpr const char* ConfigFileName = "MR-Launcher.cfg";

// Ruta del cfg para un launcher cuyo ejecutable esta en ExeDir. No consulta el
// directorio de trabajo.
std::filesystem::path ConfigPath(const std::filesystem::path& ExeDir);

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

// Borra los temporales de escritura que dejo una ejecucion caida entre escribir y renombrar:
// archivos regulares con el patron exacto <nombre del cfg>.<pid>.tmp (pid = solo digitos) junto
// al cfg, con mas de MinAge sin modificarse (uno mas reciente puede ser de otro launcher que
// esta escribiendo ahora). Contienen la clave de instalacion: no se lee ni se registra su
// contenido. Nunca toca el cfg, su .corrupt ni nada que no encaje; nunca lanza.
// Devuelve cuantos borro.
std::size_t RemoveStaleConfigTemps(const std::filesystem::path& CfgFile, std::chrono::seconds MinAge = std::chrono::seconds(60));

// Valida y guarda el nombre. Si es invalido deja Reason y no toca el archivo.
bool SetPilotName(const std::filesystem::path& CfgFile, std::string_view Name, std::string& Reason);

// Borra el nombre guardado (queda "" y el resto del cfg se conserva). Si no puede
// guardar, deja Reason y no toca el archivo.
bool ClearPilotName(const std::filesystem::path& CfgFile, std::string& Reason);

// {"name":"...","install":"..."} (JSON UTF-8, <= 256 bytes con valores validos).
std::string IdentityPacket(const LauncherIdentity& Identity);

// Clave "Transport" del cfg: "auto" (por defecto; el launcher decide) o "tcp"
// (fuerza el fallback a TCP desde el inicio). Cualquier otro valor se repara a "auto".
// true solo si Doc["Transport"] es exactamente la cadena "tcp"; todo lo demas
// (ausente, "auto", otras cadenas, no cadenas) da false. Acepta el JSON que tenga
// el llamador (json u ordered_json).
bool ForceTcpFromConfig(const nlohmann::json& Doc);
bool ForceTcpFromConfig(const nlohmann::ordered_json& Doc);

namespace detail {
    using KeyGenerator = std::function<std::string()>;
    // Igual que LoadOrCreateIdentity pero con generador inyectable (pruebas).
    LauncherIdentity LoadOrCreateIdentityWith(const std::filesystem::path& CfgFile, const KeyGenerator& Generate);

    // Id del proceso (el que lleva el temporal de escritura del cfg).
    unsigned long CurrentProcessId();
    // Temporal de escritura del cfg: <cfg>.<pid>.tmp, unico por proceso.
    std::filesystem::path TempConfigPath(const std::filesystem::path& File, unsigned long Pid);
    // Escribe Doc en Tmp, lo vuelca a disco y lo renombra sobre File. Si lo logra, Tmp ya no
    // existe; si falla tras haberlo escrito, lo borra. Si Tmp existia y no se puede abrir como
    // archivo (p. ej. es un directorio), devuelve false y NO lo toca. Expuesta para las pruebas;
    // el resto usa TempConfigPath del pid propio.
    bool WriteConfigAtomicTo(const std::filesystem::path& File, const nlohmann::ordered_json& Doc, const std::filesystem::path& Tmp);
}

}
