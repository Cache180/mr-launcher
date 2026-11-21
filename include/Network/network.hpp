/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/


#pragma once
#include "MRIdentity.h"
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

#ifdef __linux__
#include "linuxfixes.h"
#include <bits/types/siginfo_t.h>
#include <cstdint>
#include <sys/ucontext.h>
#include <arpa/inet.h>
#endif

void NetReset();
extern bool Dev;
extern int ping;

// Enlaza el puerto local del launcher (error fatal si esta ocupado). Idempotente.
bool CoreListen();
[[noreturn]] void CoreNetwork();
extern int ProxyPort;
extern int ClientID;
extern int LastPort;
extern bool ModLoaded;
extern bool Terminate;
extern uint64_t UDPSock;
extern uint64_t TCPSock;
extern std::filesystem::path CachingDirectory;
extern bool deleteDuplicateMods;
extern bool TCPTerminate;
extern std::string LastIP;
extern std::string MStatus;
extern std::string UlStatus;
extern std::string InstallKey;
extern std::string Username;
extern std::filesystem::path ConfigFile;
extern nlohmann::json ConfiguredServers;
extern std::string magic;
int KillSocket(uint64_t Dead);
void UUl(const std::string& R);
void UDPSend(std::string Data);
bool CheckBytes(int32_t Bytes, int32_t Expected = -1);
void GameSend(std::string_view Data);
void SendLarge(std::string Data);
std::string TCPRcv(uint64_t Sock);
void SyncResources(uint64_t TCPSock, const MR::LauncherIdentity& Identity);
std::string GetAddr(const std::string& IP);
void ServerParser(std::string_view Data);
// Fallback a TCP (MRTransport): tick del monitor y bandera del cfg `Transport: "tcp"`.
void TransportTick();
void SendTransportFt();
extern bool ForceTcpTransport;
void TCPSend(const std::string& Data, uint64_t Sock);
void TCPClientMain(const std::string& IP, int Port, MR::LauncherIdentity Identity);
void UDPClientMain(const std::string& IP, int Port);
void TCPGameServer(const std::string& IP, int Port, MR::LauncherIdentity Identity);
bool SecurityWarning();
void CoreSend(std::string data);
int RecvWaitAll(int sockfd, char *buf, int len);
std::string QueryServerInfo(const std::string& Host, int Port);
