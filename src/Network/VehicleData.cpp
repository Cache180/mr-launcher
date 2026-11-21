/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Network/network.hpp"
#include "Zlib/Compressor.h"
#include <stdexcept>

#if defined(_WIN32)
#include <ws2tcpip.h>
#elif defined(__linux__)
#include "linuxfixes.h"
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include "Logger.h"
#include "MRTransport.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

SOCKET UDPSock = -1;
sockaddr_in* ToServer = nullptr;

void UDPSend(std::string Data) {
    if (ClientID == -1 || UDPSock == -1)
        return;
    if (Data.length() > 400) {
        auto res = Comp(std::span<char>(Data.data(), Data.size()));
        Data = "ABG:" + std::string(res.data(), res.size());
    }
    std::string Packet = char(ClientID + 1) + std::string(":") + Data;
    int sendOk = sendto(UDPSock, Packet.c_str(), int(Packet.size()), 0, (sockaddr*)ToServer, sizeof(*ToServer));
    if (sendOk == SOCKET_ERROR)
        error("Error Code : " + std::to_string(WSAGetLastError()));
}

void SendLarge(std::string Data) {
    if (Data.length() > 400) {
        auto res = Comp(std::span<char>(Data.data(), Data.size()));
        Data = "ABG:" + std::string(res.data(), res.size());
    }
    TCPSend(Data, TCPSock);
}

void UDPParser(std::string_view Packet) {
    if (Packet.substr(0, 4) == "ABG:") {
        auto substr = Packet.substr(4);
        try {
            auto res = DeComp(std::span<const char>(substr.data(), substr.size()));
            std::string DeCompPacket = std::string(res.data(), res.size());
            ServerParser(DeCompPacket);
        } catch (const std::runtime_error& err) {
            error("Error in decompression of UDP, ignoring");
        }
    } else {
        ServerParser(Packet);
    }
}
void UDPRcv(std::uint64_t Generation) {
    sockaddr_in FromServer {};
#if defined(_WIN32)
    int clientLength = sizeof(FromServer);
#elif defined(__linux__)
    socklen_t clientLength = sizeof(FromServer);
#endif
    ZeroMemory(&FromServer, clientLength);
    static thread_local std::array<char, 10240> Ret {};
    if (UDPSock == -1)
        return;
    int32_t Rcv = recvfrom(UDPSock, Ret.data(), Ret.size() - 1, 0, (sockaddr*)&FromServer, &clientLength);
    // Error o espera agotada (1 s sin datagrama): no es un fallo; el bucle vuelve a mirar Terminate.
    if (Rcv == SOCKET_ERROR)
        return;
    if (MR::ActiveTransport().OnUdpPacket(Generation, std::chrono::steady_clock::now()))
        info("Conexion: UDP");
    Ret[Rcv] = 0;
    UDPParser(std::string_view(Ret.data(), Rcv));
}
void UDPClientMain(const std::string& IP, int Port) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(514, &data)) {
        error("Can't start Winsock!");
        return;
    }
#endif

    delete ToServer;
    ToServer = new sockaddr_in;
    ToServer->sin_family = AF_INET;
    ToServer->sin_port = htons(Port);
    inet_pton(AF_INET, IP.c_str(), &ToServer->sin_addr);
    UDPSock = socket(AF_INET, SOCK_DGRAM, 0);
    // Espera de recepcion de 1 s: el bucle no queda bloqueado para siempre si no llega nada
    // y puede terminar al cerrarse la sesion aunque nadie cierre el socket.
#if defined(_WIN32)
    DWORD RcvTimeout = 1000;
#else
    timeval RcvTimeout { 1, 0 };
#endif
    if (setsockopt(UDPSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&RcvTimeout), sizeof(RcvTimeout)) != 0)
        warn("No se pudo fijar la espera de recepcion UDP: " + std::to_string(WSAGetLastError()));
    // Un monitor por sesion (nace limpio; solo el cfg decide si arranca forzado a TCP).
    // Con Transport "tcp" el primer tick pasa a TCP ya (Ft antes que cualquier otro envio)
    // y no se manda nada por UDP.
    const std::uint64_t Generation = MR::ActiveTransport().Begin(ForceTcpTransport, std::chrono::steady_clock::now());
    TransportTick();
    const bool UdpUsable = MR::ActiveTransport().Current() == MR::Transport::Udp;
    if (UdpUsable && !magic.empty())
        for (int i = 0; i < 10; i++)
            UDPSend(magic);
    GameSend("P" + std::to_string(ClientID));
    TCPSend("H", TCPSock);
    if (UdpUsable)
        UDPSend("p");
    debug("Starting UDP receive loop");
    while (!Terminate) {
        UDPRcv(Generation);
    }
    debug("UDP receive loop done");
    MR::ActiveTransport().End(Generation);
    KillSocket(UDPSock);
    WSACleanup();
}
