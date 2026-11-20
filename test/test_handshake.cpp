/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRHandshake.h"
#include "MRIdentity.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <deque>
#include <string>
#include <vector>

namespace {

const std::string ValidInstall(48, 'c');

MR::LauncherIdentity ValidIdentity() {
    return { "Piloto_1", ValidInstall };
}

// Transporte con guion: contesta lo que se le pida, en orden, y anota todo lo que
// se le envia. Sin respuestas pendientes se comporta como un socket cerrado.
struct ScriptedServer {
    std::deque<std::string> Replies;
    std::vector<std::string> Sent;
    bool FailASend = false; // si true, falla el envio numero SendFailsAt (0 = el primero)
    std::size_t SendFailsAt = 0;
    std::size_t ReceiveCalls = 0;

    MR::HandshakeTransport Transport() {
        MR::HandshakeTransport T;
        T.Send = [this](const std::string& Data) {
            const bool Fail = FailASend && Sent.size() == SendFailsAt;
            Sent.push_back(Data);
            return !Fail;
        };
        T.Receive = [this]() -> std::string {
            ++ReceiveCalls;
            if (Replies.empty()) {
                return {};
            }
            std::string Next = std::move(Replies.front());
            Replies.pop_front();
            return Next;
        };
        return T;
    }
};

MR::HandshakeResult Run(ScriptedServer& Server, const MR::LauncherIdentity& Identity = ValidIdentity()) {
    return MR::PerformHandshake(Identity, "1.0", Server.Transport());
}

} // namespace

TEST_CASE("handshake: primero manda MR + version, despues la identidad") {
    ScriptedServer Server;
    Server.Replies = { "A", "P7" };
    const auto Result = Run(Server);
    REQUIRE(Server.Sent.size() == 2);
    CHECK(Server.Sent[0] == "MR1.0");
    const auto Json = nlohmann::json::parse(Server.Sent[1], nullptr, false);
    REQUIRE(Json.is_object());
    CHECK(Json.at("name") == "Piloto_1");
    CHECK(Json.at("install") == ValidInstall);
    CHECK(Server.Sent[1].size() <= 256);
    CHECK(Server.Sent[1] == MR::IdentityPacket(ValidIdentity()));
    CHECK(Result.Ok);
}

TEST_CASE("handshake: adopta el id de P<id>") {
    ScriptedServer Server;
    Server.Replies = { "A", "P7" };
    const auto Result = Run(Server);
    CHECK(Result.Ok);
    CHECK(Result.ClientId == 7);
    CHECK(Result.Failure.empty());
}

TEST_CASE("handshake: espera la respuesta a cada paquete antes de mandar el siguiente") {
    ScriptedServer Server;
    Server.Replies = { "A", "P1" };
    Run(Server);
    CHECK(Server.ReceiveCalls == 2);
}

TEST_CASE("handshake: la expulsion tras la identidad llega con su texto intacto") {
    ScriptedServer Server;
    Server.Replies = { "A", "KNombre en uso: elige otro nombre de piloto" };
    const auto Result = Run(Server);
    CHECK_FALSE(Result.Ok);
    CHECK(Result.Failure == "Nombre en uso: elige otro nombre de piloto");
}

TEST_CASE("handshake: la expulsion por cabecera corta el saludo sin mandar identidad") {
    ScriptedServer Server;
    Server.Replies = { "KMR Launcher desactualizado: se requiere la version >= 1.0.0", "P7" };
    const auto Result = Run(Server);
    CHECK_FALSE(Result.Ok);
    CHECK(Result.Failure == "MR Launcher desactualizado: se requiere la version >= 1.0.0");
    CHECK(Server.Sent.size() == 1);
}

TEST_CASE("handshake: respuesta E se trata como fallo con su texto") {
    SUBCASE("a la version") {
        ScriptedServer Server;
        Server.Replies = { "Eservidor lleno", "P7" };
        const auto Result = Run(Server);
        CHECK_FALSE(Result.Ok);
        CHECK(Result.Failure == "servidor lleno");
        CHECK(Server.Sent.size() == 1);
    }
    SUBCASE("a la identidad") {
        ScriptedServer Server;
        Server.Replies = { "A", "Eservidor lleno" };
        const auto Result = Run(Server);
        CHECK_FALSE(Result.Ok);
        CHECK(Result.Failure == "servidor lleno");
    }
    SUBCASE("sin texto hay un mensaje de reserva") {
        ScriptedServer Server;
        Server.Replies = { "A", "E" };
        const auto Result = Run(Server);
        CHECK_FALSE(Result.Ok);
        CHECK_FALSE(Result.Failure.empty());
    }
}

TEST_CASE("handshake: sin respuesta falla limpio y no sigue") {
    SUBCASE("a la version") {
        ScriptedServer Server;
        const auto Result = Run(Server);
        CHECK_FALSE(Result.Ok);
        CHECK(Server.Sent.size() == 1);
        CHECK(Server.ReceiveCalls == 1);
    }
    SUBCASE("a la identidad") {
        ScriptedServer Server;
        Server.Replies = { "A" };
        const auto Result = Run(Server);
        CHECK_FALSE(Result.Ok);
        CHECK(Server.Sent.size() == 2);
        CHECK(Server.ReceiveCalls == 2);
    }
}

TEST_CASE("handshake: respuestas basura a la version se rechazan") {
    for (const std::string& Garbage : { std::string("X"), std::string("AB"), std::string("a"), std::string("P7"), std::string("\0", 1) }) {
        ScriptedServer Server;
        Server.Replies = { Garbage, "P7" };
        const auto Result = Run(Server);
        CHECK_FALSE(Result.Ok);
        CHECK_FALSE(Result.Failure.empty());
        CHECK(Server.Sent.size() == 1); // la identidad no se manda a quien no contesto `A`
    }
}

TEST_CASE("handshake: P<id> mal formado se rechaza") {
    for (const std::string& Garbage : { std::string("P"), std::string("Pabc"), std::string("P-1"), std::string("P7x"),
             std::string("P 7"), std::string("P99999999999999999999"), std::string("Q7"), std::string("7") }) {
        ScriptedServer Server;
        Server.Replies = { "A", Garbage };
        const auto Result = Run(Server);
        CHECK_MESSAGE(!Result.Ok, "acepto: ", Garbage);
        CHECK_MESSAGE(!Result.Failure.empty(), "sin mensaje para: ", Garbage);
    }
}

TEST_CASE("handshake: acepta ids numericos acotados") {
    ScriptedServer Zero;
    Zero.Replies = { "A", "P0" };
    const auto ZeroResult = Run(Zero);
    CHECK(ZeroResult.Ok);
    CHECK(ZeroResult.ClientId == 0);
    ScriptedServer Big;
    Big.Replies = { "A", "P999999999" };
    const auto BigResult = Run(Big);
    CHECK(BigResult.Ok);
    CHECK(BigResult.ClientId == 999999999);
}

TEST_CASE("handshake: la razon de una expulsion se acota y no corta un caracter UTF-8") {
    std::string Long = "K";
    Long.append(MR::MaxKickReasonBytes - 1, 'a');
    Long += "\xC3\xB1"; // n con tilde: cruza el tope
    Long.append(500, 'b');
    ScriptedServer Server;
    Server.Replies = { Long };
    const auto Result = Run(Server);
    CHECK_FALSE(Result.Ok);
    CHECK(Result.Failure.size() == MR::MaxKickReasonBytes - 1); // el caracter partido se descarta entero
    CHECK(Result.Failure.find('b') == std::string::npos);
}

TEST_CASE("handshake: sin nombre de piloto no manda nada y avisa con el texto exacto") {
    ScriptedServer Server;
    Server.Replies = { "A", "P7" };
    const auto Result = Run(Server, { "", ValidInstall });
    CHECK_FALSE(Result.Ok);
    CHECK(Result.Failure == "Elige un nombre de piloto antes de entrar");
    CHECK(std::string(MR::NoPilotNameMessage) == "Elige un nombre de piloto antes de entrar");
    CHECK(Server.Sent.empty());
    CHECK(Server.ReceiveCalls == 0);
}

TEST_CASE("handshake: un nombre invalido tampoco conecta") {
    for (const std::string& Name : { std::string("ab"), std::string("con espacio"), std::string(21, 'x'), std::string("a\"b") }) {
        ScriptedServer Server;
        Server.Replies = { "A", "P7" };
        const auto Result = Run(Server, { Name, ValidInstall });
        CHECK_FALSE(Result.Ok);
        CHECK(Result.Failure == MR::NoPilotNameMessage);
        CHECK(Server.Sent.empty());
    }
}

TEST_CASE("handshake: una clave de instalacion invalida no conecta ni se menciona") {
    for (const std::string& Install : { std::string(), std::string(31, 'a'), std::string(32, '-') }) {
        ScriptedServer Server;
        Server.Replies = { "A", "P7" };
        const auto Result = Run(Server, { "Piloto_1", Install });
        CHECK_FALSE(Result.Ok);
        CHECK_FALSE(Result.Failure.empty());
        CHECK(Server.Sent.empty());
        if (!Install.empty()) {
            CHECK(Result.Failure.find(Install) == std::string::npos);
        }
    }
}

TEST_CASE("handshake: si falla un envio no se sigue") {
    SUBCASE("el de la version") {
        ScriptedServer Server;
        Server.Replies = { "A", "P7" };
        Server.FailASend = true;
        Server.SendFailsAt = 0;
        CHECK_FALSE(Run(Server).Ok);
        CHECK(Server.Sent.size() == 1);
        CHECK(Server.ReceiveCalls == 0);
    }
    SUBCASE("el de la identidad") {
        ScriptedServer Server;
        Server.Replies = { "A", "P7" };
        Server.FailASend = true;
        Server.SendFailsAt = 1;
        CHECK_FALSE(Run(Server).Ok);
        CHECK(Server.Sent.size() == 2);
        CHECK(Server.ReceiveCalls == 1);
    }
}

TEST_CASE("handshake: ningun mensaje de fallo incluye la clave de instalacion") {
    ScriptedServer Server;
    Server.Replies = { "A", "Pbasura" };
    const auto Result = Run(Server);
    CHECK_FALSE(Result.Ok);
    CHECK(Result.Failure.find(ValidInstall) == std::string::npos);
}
