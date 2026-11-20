/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Lista de servidores del launcher: sale de la clave `Servers` de MR-Launcher.cfg
// (no hay lista maestra). Cada entrada se completa con la respuesta de la
// consulta `I` del server. Modulo puro: no depende de los globales, el logger ni
// los sockets del launcher; solo BuildServerList (src/Network/ServerInfo.cpp)
// toca la red.

#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace MR {

// Mas entradas que esta no se consultan (cada una usa un hilo).
constexpr std::size_t MaxServers = 64;

// {name?, ip, port}: ip es un host o IPv4 (letras, digitos, '.', '-', '_'; 1-253
// caracteres), port un entero 1-65535 y name, si esta, una cadena.
bool IsValidServerCfg(const nlohmann::json& Cfg);

// Entrada de la lista de servidores del mod. Cfg = {name, ip, port};
// InfoJson = respuesta de la consulta `I`, o "" si el server no contesto. Una
// respuesta que no es un objeto JSON se trata como vacia. Devuelve siempre las
// 19 claves del mod; los valores de texto son cadenas, como en la lista oficial.
nlohmann::json BuildServerEntry(const nlohmann::json& Cfg, const std::string& InfoJson);

// Consulta `I` a Host:Port. Devuelve el JSON de respuesta o "" si fallo o
// vencio el tiempo. Puede lanzar: se trata como "".
using InfoQuery = std::function<std::string(const std::string& Host, int Port)>;

// Arreglo JSON (texto) con una entrada por server valido de Servers, en orden y
// con las consultas en paralelo. Las entradas invalidas se omiten.
std::string BuildServerListWith(const nlohmann::json& Servers, const InfoQuery& Query);

// BuildServerListWith con la consulta real del launcher (QueryServerInfo).
std::string BuildServerList(const nlohmann::json& Servers);

}
