/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Lo que el launcher decide a partir de lo que dice el juego por el canal local:
// que enlaces abre y el protocolo de identidad (`Nc`, `N:{"name":...}`, `N:LO`).
// Todo lo que llega del juego es entrada no confiable. Modulo puro: no depende
// de los globales, el logger ni los sockets del launcher.

#include <filesystem>
#include <string>
#include <string_view>

namespace MR {

// Solo https://muertazosracing.com (el host entero, no un prefijo de otro),
// https://discord.gg/ y https://github.com/Cache180/. Sin espacios, controles,
// '\' ni "..".
bool IsAllowedLink(std::string_view Link);

// Atiende un mensaje del canal de identidad (Data empieza por 'N') y devuelve el
// paquete a enviar al juego, o "" si no hay respuesta que dar.
//   Nc            -> N{"Auth":1,"username":"<Name>","role":"USER","id":0} (N{"Auth":0} sin nombre)
//   N:{"name":x}  -> guarda el nombre con SetPilotName; N{"success":true,...} o N{"success":false,"message":...}
//   N:LO          -> borra el nombre (memoria y cfg); responde "N"
// ActiveName es el nombre de la sesion: se actualiza solo si el cambio se guardo
// (y se vacia siempre con N:LO). Nunca lanza.
std::string HandleIdentityMessage(const std::string& Data, const std::filesystem::path& CfgFile, std::string& ActiveName);

}
