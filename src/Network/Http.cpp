/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Http.h"
#include "Logger.h"
#include "MRTransport.h"
#include "Network/network.hpp"
#include <httplib.h>
#include <string>
#include <thread>

static void NotAvailable(const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.status = 404;
    res.set_content(R"({"error":"not available in MR Launcher"})", "application/json");
}

// Transporte actual de la sesion (la interfaz del juego lo consulta desde otro origen).
// Sin sesion activa responde {"transport":"udp"}.
static void CurrentTransport(const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.status = 200;
    res.set_content(MR::TransportJson(MR::ActiveTransport().Current()), "application/json");
}

// El mod pide el puerto de este servidor, pero MR no tiene backend ni foro: todo
// (/backend/*, /avatar/* y lo demas) responde 404 sin hacer ninguna peticion externa,
// salvo GET /mr/transport. Esa ruta va primero: httplib prueba los manejadores en orden.
void HTTP::StartProxy() {
    std::thread proxy([]() {
        httplib::Server HTTPProxy;
        HTTPProxy.Get("/mr/transport", CurrentTransport);
        const std::string pattern = ".*";
        HTTPProxy.Get(pattern, NotAvailable);
        HTTPProxy.Post(pattern, NotAvailable);

        ProxyPort = HTTPProxy.bind_to_any_port("127.0.0.1");
        if (ProxyPort < 0) {
            error("No se pudo abrir el servidor HTTP local: algunas funciones del juego pueden no funcionar.");
            return;
        }
        debug("HTTP Proxy listening on port " + std::to_string(ProxyPort));
        HTTPProxy.listen_after_bind();
    });
    proxy.detach();
}
