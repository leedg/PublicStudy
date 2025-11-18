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

// ============================================================
// RIO 에코 서버 메인 함수
// 
// 서버 초기화 및 실행 순서:
// 1. RIO 네트워크 초기화 (Winsock, RIO 함수 테이블 로드)
// 2. RIOWorker 스레드들 생성 및 시작
// 3. IOCPAcceptor 생성 및 시작 (클라이언트 연결 수락)
// 4. 메인 루프 (Ctrl+C 대기)
// 5. 종료 시 리소스 정리 (역순으로)
// 
// 아키텍처:
//   클라이언트 연결 -> IOCPAcceptor (AcceptEx) -> 
//   RIOWorker (Round-Robin) -> Session (RIO I/O) -> 
//   Echo 응답
// 
// 스레드 구조:
// - 메인 스레드: 초기화 및 종료 대기
// - Acceptor 스레드 (1개): 클라이언트 연결 수락
// - Worker 스레드 (N개): 세션 관리 및 I/O 처리
// ============================================================
int main(int argc, char** argv) 
{
    // *** Breakpoint 1: Program start ***
    printf("\n========================================\n");
    printf("[DEBUG] main: Program started\n");
    printf("========================================\n\n");
    
    // 포트 번호 파싱 (명령줄 인자 또는 기본값)
    uint16_t port = Config::ListenPortDefault;
    if (argc >= 2) 
    {
        int p = atoi(argv[1]);
        // 유효한 포트 범위: 1-65535
        if (p > 0 && p < 65536)
        {
            port = static_cast<uint16_t>(p);
        }
    }

    // ========== RIO 네트워크 초기화 ==========
    
    // *** Breakpoint 2: RIO network initialization start ***
    printf("[DEBUG] main: Initializing RIO network...\n");
    
    // RIO 초기화:
    // 1. Winsock 초기화 (WSAStartup)
    // 2. RIO 함수 테이블 로드 (WSAIoctl)
    // 3. IOCP 생성 (IOCP 모드용, 폴링 모드에서도 생성)
    if (!RIONetwork::Init()) 
    {
        printf("[FATAL] main: RIO network initialization failed\n");
        
        return 1;
    }
    
    // *** Breakpoint 3: RIO network initialization complete ***
    printf("[DEBUG] main: RIO network initialized\n\n");

    // ========== 워커 개수 결정 ==========
    
    // 워커 개수: Config 값 또는 CPU 코어 수
    // - CPU 코어 수만큼 워커를 만들면 일반적으로 좋은 성능
    // - 하이퍼스레딩 환경에서는 논리 코어 수를 반환
    int workerCount = Config::WorkerCount <= 0 ? (int)std::thread::hardware_concurrency() : Config::WorkerCount;
    if (workerCount <= 0)
    {
        workerCount = 1;  // 최소 1개
    }

    // ========== 워커 생성 ==========
    
    // *** Breakpoint 4: Worker creation start ***
    printf("[DEBUG] main: Creating %d workers...\n", workerCount);
    
    // unique_ptr 벡터: 워커들의 소유권 관리 (RAII)
    std::vector<std::unique_ptr<RIOWorker>> workers;
    workers.reserve(workerCount);
    
    // 각 워커 생성 및 시작
    for (int i = 0; i < workerCount; ++i) 
    {
        // *** Breakpoint 5: Creating each worker ***
        printf("[DEBUG] main: Creating worker[%d]...\n", i);
        
        // RIOWorker 생성:
        // - Completion Queue 생성
        // - 버퍼 풀 초기화
        // - 워커 스레드 시작 (run() 함수 실행)
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

    // ========== Acceptor 생성 및 시작 ==========
    
    // *** Breakpoint 7: Acceptor creation and start ***
    printf("[DEBUG] main: Creating and starting acceptor (port=%u)...\n", port);
    
    // IOCPAcceptor 생성:
    // - 리스닝 소켓 생성 및 바인딩
    // - AcceptEx 함수 포인터 로드
    // - IOCP 생성
    // - 여러 개의 AcceptEx 요청 등록
    // - Acceptor 스레드 시작
    IOCPAcceptor acceptor(port, workers);
    
    if (!acceptor.start()) 
    {
        printf("[FATAL] main: Acceptor start failed\n");
        
        return 1;
    }

    // ========== 서버 준비 완료 ==========
    
    // *** Breakpoint 8: Server ready ***
    printf("\n========================================\n");
    printf("[LISTEN] tcp://0.0.0.0:%u  (RIO echo, %d workers, polling=%s)\n",
           port, workerCount, Config::UsePolling ? "enabled" : "disabled");
    printf("[DEBUG] main: Server ready - waiting for client connections...\n");
    printf("========================================\n\n");

    // ========== 메인 루프 (종료 대기) ==========
    
    // *** Breakpoint 9: Main loop entry ***
    printf("[DEBUG] main: Entering main loop (Ctrl+C to exit)\n\n");
    
    // Acceptor가 실행 중인 동안 계속 대기
    // - 실제 작업은 Acceptor 스레드와 Worker 스레드들이 수행
    // - 메인 스레드는 여기서 대기만 함
    // - Ctrl+C 등으로 종료 시 acceptor.stop()이 호출되어 루프 탈출
    while (acceptor.running()) 
    {
        // 500ms마다 종료 플래그 확인
        // (너무 짧으면 CPU 낭비, 너무 길면 종료 응답성 저하)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ========== 종료 프로세스 ==========
    
    // *** Breakpoint 10: Shutdown process start ***
    printf("\n[DEBUG] main: Starting shutdown process...\n");
    
    // *** Breakpoint 11: Stopping acceptor ***
    printf("[DEBUG] main: Stopping acceptor...\n");
    // Acceptor 중지:
    // - 스레드 종료 신호
    // - AcceptEx 소켓들 닫기
    // - IOCP 닫기
    // - 리스닝 소켓 닫기
    acceptor.stop();
    printf("[DEBUG] main: Acceptor stopped\n");
    
    // *** Breakpoint 12: Stopping workers ***
    printf("[DEBUG] main: Stopping workers...\n");
    
    // 모든 워커 중지
    // - 스레드 종료 신호
    // - 모든 세션 정리 (소켓 닫기)
    // - Completion Queue 닫기
    // - 버퍼 풀 정리
    for (auto& w : workers) 
    {
        w->stop();
    }
    
    // unique_ptr 벡터 정리 (워커 객체 자동 소멸)
    workers.clear();
    printf("[DEBUG] main: All workers stopped\n");

    // *** Breakpoint 13: RIO network shutdown ***
    printf("[DEBUG] main: Shutting down RIO network...\n");
    // RIO 네트워크 종료:
    // - IOCP 핸들 닫기
    // - Winsock 정리 (WSACleanup)
    RIONetwork::Shutdown();
    printf("[DEBUG] main: RIO network shut down\n");
    
    // *** Breakpoint 14: Program exit ***
    printf("\n========================================\n");
    printf("[DEBUG] main: Program terminated normally\n");
    printf("========================================\n\n");
    
    return 0;
}
