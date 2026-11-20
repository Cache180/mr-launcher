/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

// Consulta `I` a un server (la misma que usa el juego para la lista) y la lista
// de servidores del cfg. Es la parte del modulo MRServers que toca la red; el
// resto (MRServers.cpp) es puro y se prueba aparte.

#include "Logger.h"
#include "MRServers.h"
#include "Network/network.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__)
#include "linuxfixes.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

// Tope de toda la consulta (conexion + respuesta): un server caido o lento no
// puede colgar la lista.
constexpr std::chrono::seconds QueryTimeout { 5 };
// La respuesta `I` mide unos cientos de bytes; el tope evita reservar memoria
// por un encabezado mentiroso.
constexpr int32_t MaxInfoBytes = 1024 * 1024;

std::chrono::milliseconds Remaining(Clock::time_point Deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Deadline - Clock::now());
}

bool SetBlocking(SOCKET Sock, bool Blocking) {
#if defined(_WIN32)
    u_long Mode = Blocking ? 0 : 1;
    return ioctlsocket(Sock, FIONBIO, &Mode) == 0;
#else
    const int Flags = fcntl(static_cast<int>(Sock), F_GETFL, 0);
    if (Flags < 0) {
        return false;
    }
    return fcntl(static_cast<int>(Sock), F_SETFL, Blocking ? (Flags & ~O_NONBLOCK) : (Flags | O_NONBLOCK)) == 0;
#endif
}

bool SetRecvTimeout(SOCKET Sock, std::chrono::milliseconds Timeout) {
#if defined(_WIN32)
    const DWORD Ms = static_cast<DWORD>(Timeout.count());
    return setsockopt(Sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&Ms), sizeof(Ms)) == 0;
#else
    timeval Tv {};
    Tv.tv_sec = static_cast<time_t>(Timeout.count() / 1000);
    Tv.tv_usec = static_cast<suseconds_t>((Timeout.count() % 1000) * 1000);
    return setsockopt(static_cast<int>(Sock), SOL_SOCKET, SO_RCVTIMEO, &Tv, sizeof(Tv)) == 0;
#endif
}

bool ConnectBefore(SOCKET Sock, const sockaddr_in& Addr, Clock::time_point Deadline) {
    if (!SetBlocking(Sock, false)) {
        return false;
    }
    if (connect(Sock, reinterpret_cast<const sockaddr*>(&Addr), sizeof(Addr)) != 0) {
#if defined(_WIN32)
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            return false;
        }
#else
        if (errno != EINPROGRESS) {
            return false;
        }
#endif
        const auto Left = Remaining(Deadline);
        if (Left.count() <= 0) {
            return false;
        }
        fd_set Writable, Failed;
        FD_ZERO(&Writable);
        FD_ZERO(&Failed);
        FD_SET(Sock, &Writable);
        FD_SET(Sock, &Failed);
        timeval Tv {};
        Tv.tv_sec = static_cast<long>(Left.count() / 1000);
        Tv.tv_usec = static_cast<long>((Left.count() % 1000) * 1000);
        // En Windows un connect fallido se avisa en el conjunto de errores.
        if (select(static_cast<int>(Sock) + 1, nullptr, &Writable, &Failed, &Tv) <= 0 || FD_ISSET(Sock, &Failed)) {
            return false;
        }
        int SocketError = 0;
        socklen_t Length = sizeof(SocketError);
        if (getsockopt(Sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&SocketError), &Length) != 0 || SocketError != 0) {
            return false;
        }
    }
    return SetBlocking(Sock, true);
}

bool RecvExactBefore(SOCKET Sock, char* Buffer, size_t Length, Clock::time_point Deadline) {
    size_t Got = 0;
    while (Got < Length) {
        const auto Left = Remaining(Deadline);
        if (Left.count() <= 0 || !SetRecvTimeout(Sock, Left)) {
            return false;
        }
        const int Received = recv(Sock, Buffer + Got, static_cast<int>(Length - Got), 0);
        if (Received <= 0) {
            return false;
        }
        Got += static_cast<size_t>(Received);
    }
    return true;
}

// Envia `I` y lee la respuesta: encabezado de 4 bytes con el largo y el JSON.
std::string RequestInfo(SOCKET Sock, Clock::time_point Deadline) {
    const char Code = 'I';
    if (send(Sock, &Code, 1, 0) != 1) {
        return {};
    }
    int32_t Header = 0;
    if (!RecvExactBefore(Sock, reinterpret_cast<char*>(&Header), sizeof(Header), Deadline)) {
        return {};
    }
    if (Header <= 0 || Header > MaxInfoBytes) {
        return {};
    }
    std::string Info(static_cast<size_t>(Header), '\0');
    if (!RecvExactBefore(Sock, Info.data(), Info.size(), Deadline)) {
        return {};
    }
    return Info;
}

}

std::string QueryServerInfo(const std::string& Host, int Port) {
    if (Port < 1 || Port > 65535) {
        debug("Invalid port number: " + std::to_string(Port));
        return {};
    }
    const std::string IP = GetAddr(Host);
    if (IP.find('.') == std::string::npos) {
        if (IP == "DNS")
            warn("Connection Failed! (DNS Lookup Failed) for " + Host);
        else
            warn("Connection Failed! (WSA failed to start) for " + Host);
        return {};
    }
    sockaddr_in ServerAddr {};
    ServerAddr.sin_family = AF_INET;
    ServerAddr.sin_port = htons(static_cast<uint16_t>(Port));
    if (inet_pton(AF_INET, IP.c_str(), &ServerAddr.sin_addr) != 1) {
        return {};
    }

    const SOCKET Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Sock == static_cast<SOCKET>(-1)) {
        debug("Socket creation failed with error: " + std::to_string(WSAGetLastError()));
        return {};
    }
    const auto Deadline = Clock::now() + QueryTimeout;
    std::string Info;
    if (ConnectBefore(Sock, ServerAddr, Deadline)) {
        Info = RequestInfo(Sock, Deadline);
    }
    if (Info.empty()) {
        debug("Failed to receive server info from " + Host + ":" + std::to_string(Port));
    }
    KillSocket(Sock);
    return Info;
}

std::string MR::BuildServerList(const nlohmann::json& Servers) {
    return BuildServerListWith(Servers, QueryServerInfo);
}
