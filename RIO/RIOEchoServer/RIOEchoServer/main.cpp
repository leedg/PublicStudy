// UTF-8 인코딩 워닝 억제
#pragma warning(disable: 4566)

#include "RIONetwork.h"
#include "IOCPAcceptor.h"
#include "RIOWorker.h"
#include "Config.h"
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) 
{
    // *** Breakpoint 1: Program start ***
    printf("\n========================================\n");
    printf("[DEBUG] main: Program started\n");
    printf("========================================\n\n");
    
    uint16_t port = Config::ListenPortDefault;
    if (argc >= 2) 
    {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536)
        {
            port = static_cast<uint16_t>(p);
        }
    }

    // *** Breakpoint 2: RIO network initialization start ***
    printf("[DEBUG] main: Initializing RIO network...\n");
    
    if (!RIONetwork::Init()) 
    {
        printf("[FATAL] main: RIO network initialization failed\n");
        
        return 1;
    }
    
    // *** Breakpoint 3: RIO network initialization complete ***
    printf("[DEBUG] main: RIO network initialized\n\n");

    int workerCount = Config::WorkerCount <= 0 ? (int)std::thread::hardware_concurrency() : Config::WorkerCount;
    if (workerCount <= 0)
    {
        workerCount = 1;
    }

    // *** Breakpoint 4: Worker creation start ***
    printf("[DEBUG] main: Creating %d workers...\n", workerCount);
    
    std::vector<std::unique_ptr<RIOWorker>> workers;
    workers.reserve(workerCount);
    
    for (int i = 0; i < workerCount; ++i) 
    {
        // *** Breakpoint 5: Creating each worker ***
        printf("[DEBUG] main: Creating worker[%d]...\n", i);
        
        workers.emplace_back(std::make_unique<RIOWorker>(i));
        
        if (!workers.back()->start()) 
        {
            printf("[FATAL] main: Worker %d start failed\n", i);
            
            return 1;
        }
        
        printf("[DEBUG] main: Worker[%d] started\n", i);
    }
    
    // *** Breakpoint 6: All workers created ***
    printf("[DEBUG] main: All workers created (%d workers)\n\n", workerCount);

    // *** Breakpoint 7: Acceptor creation and start ***
    printf("[DEBUG] main: Creating and starting acceptor (port=%u)...\n", port);
    
    IOCPAcceptor acceptor(port, workers);
    
    if (!acceptor.start()) 
    {
        printf("[FATAL] main: Acceptor start failed\n");
        
        return 1;
    }

    // *** Breakpoint 8: Server ready ***
    printf("\n========================================\n");
    printf("[LISTEN] tcp://0.0.0.0:%u  (RIO echo, %d workers, polling=%s)\n",
           port, workerCount, Config::UsePolling ? "enabled" : "disabled");
    printf("[DEBUG] main: Server ready - waiting for client connections...\n");
    printf("========================================\n\n");

    // *** Breakpoint 9: Main loop entry ***
    printf("[DEBUG] main: Entering main loop (Ctrl+C to exit)\n\n");
    
    while (acceptor.running()) 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // *** Breakpoint 10: Shutdown process start ***
    printf("\n[DEBUG] main: Starting shutdown process...\n");
    
    // *** Breakpoint 11: Stopping acceptor ***
    printf("[DEBUG] main: Stopping acceptor...\n");
    acceptor.stop();
    printf("[DEBUG] main: Acceptor stopped\n");
    
    // *** Breakpoint 12: Stopping workers ***
    printf("[DEBUG] main: Stopping workers...\n");
    
    for (auto& w : workers) 
    {
        w->stop();
    }
    
    workers.clear();
    printf("[DEBUG] main: All workers stopped\n");

    // *** Breakpoint 13: RIO network shutdown ***
    printf("[DEBUG] main: Shutting down RIO network...\n");
    RIONetwork::Shutdown();
    printf("[DEBUG] main: RIO network shut down\n");
    
    // *** Breakpoint 14: Program exit ***
    printf("\n========================================\n");
    printf("[DEBUG] main: Program terminated normally\n");
    printf("========================================\n\n");
    
    return 0;
}
