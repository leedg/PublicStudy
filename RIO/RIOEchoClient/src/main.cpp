#include "RIOClient.h"
#include "echo_message.pb.h"
#include "RIONetwork.h"

#include <cstdio>
#include <string>
#include <iostream>

namespace {

void PrintUsage() {
    printf("Usage: RIOEchoClient.exe [host] [port]\n");
    printf("Default: host=127.0.0.1 port=5050\n\n");
    printf("Commands:\n");
    printf("  Type your message and press Enter to send\n");
    printf("  Type 'quit' or 'exit' to disconnect and quit\n\n");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (!RIONetwork::Init()) {
            return 1;
        }

        std::string host = "127.0.0.1";
        uint16_t port = 5050;

        if (argc >= 2) {
            if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
                PrintUsage();
                RIONetwork::Shutdown();
                return 0;
            }
            host = argv[1];
        }
        if (argc >= 3) {
            port = static_cast<uint16_t>(std::stoi(argv[2]));
        }

        printf("[CLIENT] Connecting to %s:%u...\n", host.c_str(), port);

        RIOClient client;
        if (!client.connectTo(host, port)) {
            printf("[CLIENT][FATAL] connectTo failed\n");
            RIONetwork::Shutdown();
            return 1;
        }

        printf("[CLIENT] Connected successfully!\n");
        printf("[CLIENT] Type your message and press Enter to send (type 'quit' or 'exit' to exit)\n\n");

        std::string line;
        while (true) {
            printf("> ");
            std::cout.flush();

            if (!std::getline(std::cin, line)) {
                if (std::cin.eof()) {
                    printf("\n[CLIENT] EOF detected, disconnecting...\n");
                } else {
                    printf("\n[CLIENT] Input error, disconnecting...\n");
                }
                break;
            }

            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) {
                continue; // Empty line
            }
            line = line.substr(start, end - start + 1);

            // Check for quit commands
            if (line == "quit" || line == "exit") {
                printf("[CLIENT] Disconnecting...\n");
                break;
            }

            if (line.empty()) {
                continue;
            }

            printf("[CLIENT] Sending message: \"%s\"\n", line.c_str());

            // Send message
            rio::echo::EchoMessage outgoing;
            outgoing.set_text(line);

            if (!client.sendEcho(outgoing)) {
                printf("[CLIENT][ERROR] sendEcho failed, connection may be lost\n");
                break;
            }

            // Receive response
            rio::echo::EchoMessage incoming;
            if (!client.receiveEcho(incoming, 5000)) {
                printf("[CLIENT][ERROR] receiveEcho failed, connection may be lost\n");
                break;
            }

            printf("[RESPONSE] %s\n\n", incoming.text().c_str());
        }

        RIONetwork::Shutdown();
        return 0;
    }
    catch (const std::exception& e) {
        printf("[CLIENT][EXCEPTION] Caught exception: %s\n", e.what());
        RIONetwork::Shutdown();
        return 1;
    }
    catch (...) {
        printf("[CLIENT][EXCEPTION] Caught unknown exception\n");
        RIONetwork::Shutdown();
        return 1;
    }
}
