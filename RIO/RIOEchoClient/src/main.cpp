#include "RIOClient.h"
#include "echo_message.pb.h"
#include "RIONetwork.h"

#include <cstdio>
#include <string>

namespace {

void PrintUsage() {
    printf("Usage: RIOEchoClient.exe [host] [port] [message]\n");
    printf("Default: host=127.0.0.1 port=5050 message=Hello RIO\n\n");
}

} // namespace

int main(int argc, char** argv) {
    if (!RIONetwork::Init()) {
        return 1;
    }

    std::string host = "127.0.0.1";
    uint16_t port = 5050;
    std::string text = "Hello RIO";

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc >= 4) {
        text = argv[3];
    }

    if (argc == 2 && (host == "-h" || host == "--help")) {
        PrintUsage();
        RIONetwork::Shutdown();
        return 0;
    }

    RIOClient client;
    if (!client.connectTo(host, port)) {
        printf("[CLIENT][FATAL] connectTo failed\n");
        RIONetwork::Shutdown();
        return 1;
    }

    rio::echo::EchoMessage outgoing;
    outgoing.set_text(text);

    if (!client.sendEcho(outgoing)) {
        printf("[CLIENT][FATAL] sendEcho failed\n");
        RIONetwork::Shutdown();
        return 1;
    }

    rio::echo::EchoMessage incoming;
    if (!client.receiveEcho(incoming, 5000)) {
        printf("[CLIENT][FATAL] receiveEcho failed\n");
        RIONetwork::Shutdown();
        return 1;
    }

    printf("[CLIENT] 서버로부터 응답 수신: %s\n", incoming.text().c_str());

    RIONetwork::Shutdown();
    return 0;
}
