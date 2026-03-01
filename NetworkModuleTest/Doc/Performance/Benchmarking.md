# 성능 벤치마킹 가이드

**Version**: 1.0  
**Date**: 2026-01-27  
**Status**: Reference Document  
**Target Audience**: Performance Engineers, DevOps, Platform Maintainers

---

## Table of Contents

1. [Overview](#overview)
2. [Benchmark Scenarios](#benchmark-scenarios)
3. [Methodology](#methodology)
4. [Benchmarking Harness](#benchmarking-harness)
5. [Expected Results](#expected-results)
6. [Performance Tuning](#performance-tuning)
7. [Reporting & Analysis](#reporting--analysis)

---

## Overview

### 목적 (Purpose)

이 문서는 AsyncIOProvider 구현의 성능을 측정하고 비교하는 방법을 정의합니다.

**Key Metrics**:
- **Throughput** (ops/sec): 초당 처리 작업 수
- **Latency** (μsec): 요청부터 완료까지의 시간
- **CPU Usage** (%): CPU 사용률
- **Memory Usage** (MB): 메모리 사용량
- **Scalability**: 연결 수 증가에 따른 성능 변화

### Target Platforms

```
┌─────────────────────────────────────────────────────────┐
│ Platform        │ Backend          │ Min Version        │
├─────────────────────────────────────────────────────────┤
│ Windows 10      │ IOCP             │ 10.0               │
│ Windows 10      │ RIO              │ 10.0 (8.1+)        │
│ Ubuntu 20.04 LTS│ epoll            │ 4.4+ (always)      │
│ Ubuntu 20.04 LTS│ io_uring         │ 5.1+ (5.4 tested)  │
│ Ubuntu 22.04 LTS│ io_uring         │ 5.15 (latest)      │
│ macOS 12        │ kqueue           │ any                │
└─────────────────────────────────────────────────────────┘
```

---

## Benchmark Scenarios

### Scenario 1: Echo Server (Basic Throughput)

#### 목적
기본적인 송수신 처리량 측정

#### 설정

```
서버 구성:
  - 단일 스레드 이벤트 루프
  - 단일 포트 수신
  - 수신 후 즉시 에코 응답

클라이언트 구성:
  - 연결 수: 10, 50, 100, 500
  - 메시지 크기: 64B, 256B, 1KB, 4KB
  - 메시지 수: 100,000 per connection
  - 병렬 요청: 128 (pipelining)
```

#### 측정 항목

```cpp
struct EchoServerBenchmark
{
    // 영문: Messages per second
    // 한글: 초당 메시지 수
    uint64_t mMessagesPerSec;

    // 영문: Bytes per second
    // 한글: 초당 바이트 수
    uint64_t mBytesPerSec;

    // 영문: Average round-trip latency in microseconds
    // 한글: 평균 왕복 레이턴시 (마이크로초)
    double mAverageLatency;

    // 영문: P99 latency in microseconds
    // 한글: P99 레이턴시 (마이크로초)
    double mP99Latency;

    // 영문: P99.9 latency
    // 한글: P99.9 레이턴시
    double mP999Latency;

    // 영문: CPU usage percentage
    // 한글: CPU 사용률 (%)
    float mCpuUsage;

    // 영문: Memory usage in MB
    // 한글: 메모리 사용량 (MB)
    float mMemoryUsage;
};
```

#### 예상 결과 (Expected Results)

```
╔════════════════════════════════════════════════════════════╗
║ Echo Server: 100 Connections, 1KB Messages                 ║
╠════════════════════════════════════════════════════════════╣
║ Backend      │ Throughput  │ Avg Latency │ P99 Latency   ║
╟────────────────────────────────────────────────────────────╢
║ IOCP         │ 1.2M msg/s  │ 82 μsec     │ 180 μsec      ║
║ RIO          │ 3.6M msg/s  │ 28 μsec     │ 65 μsec       ║
║ epoll        │ 2.1M msg/s  │ 48 μsec     │ 120 μsec      ║
║ io_uring     │ 4.8M msg/s  │ 20 μsec     │ 45 μsec       ║
╚════════════════════════════════════════════════════════════╝
```

---

### Scenario 2: Connection Scaling

#### 목적
연결 수 증가에 따른 성능 저하 측정

#### 설정

```
연결 수: 1, 10, 50, 100, 500, 1000, 5000
각 시나리오:
  - 활성 메시지 비율: 10% (연결 수의 10%만 활동)
  - 메시지 크기: 256B
  - 지속 시간: 30초
```

#### 측정 항목

```
그래프: 연결 수 vs 처리량
        연결 수 vs 평균 레이턴시
        연결 수 vs CPU 사용률
        연결 수 vs 메모리 사용량
```

---

### Scenario 3: Batch Operations (RIO, io_uring 전용)

#### 목적
배치 작업의 성능 이점 측정

#### 설정

```
배치 크기: 1, 2, 4, 8, 16, 32, 64
각 배치:
  - 메시지 크기: 1KB
  - 연결 수: 100
  - 반복 횟수: 100,000
```

#### 측정 항목

```
그래프: 배치 크기 vs 처리량 개선 (%)
        배치 크기 vs 레이턴시 변화
```

---

### Scenario 4: CPU Core Scaling

#### 목적
멀티스레드 이벤트 루프의 확장성 측정

#### 설정

```
스레드 수: 1, 2, 4, 8, 16
각 스레드:
  - 별도의 AsyncIOProvider 인스턴스
  - 로드 밸런싱 (라운드 로빈)
  - 연결 수: 1000 (전체)
  - 메시지 크기: 1KB
  - 지속 시간: 60초
```

#### 측정 항목

```
그래프: 스레드 수 vs 총 처리량
        스레드 수 vs CPU 사용률 (전체)
        스레드 수 vs 상단 레이턴시
```

---

## Methodology

### 환경 준비 (Environment Setup)

#### 필수 사항

```bash
# 영문: Windows environment setup
# 한글: Windows 환경 설정

# Visual Studio 2022 또는 최신 버전
# Windows 10 SDK 설치
# 개발자 모드 활성화
```

```bash
# 영문: Linux environment setup
# 한글: Linux 환경 설정

# Ubuntu 20.04 LTS 또는 22.04 LTS
# kernel 5.1+ (io_uring 테스트용)
# gcc-11 또는 clang-13 이상

sudo apt-get install -y build-essential cmake
uname -r  # 커널 버전 확인
```

#### 시스템 튜닝

```bash
# 영문: Disable power management for consistent results
# 한글: 일관된 결과를 위해 전원 관리 비활성화

# Windows: 고성능 전원 관리 활성화
powercfg /setactive 8c5e7fda-e8bf-45a6-a6cc-4b3c619a6a61

# Linux: CPU 고정 주파수 설정
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

#### 네트워크 최적화

```bash
# 영문: Increase socket buffer sizes
# 한글: 소켓 버퍼 크기 증가

# Windows
REG ADD "HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters" /v TcpWindowSize /t REG_DWORD /d 1048576

# Linux
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"
```

### 측정 방법 (Measurement Method)

#### 해야할 것 (DO)

```cpp
// ✅ 적어도 3회 반복 측정
for (int run = 0; run < 3; ++run)
{
    // 워밍업
    for (int i = 0; i < 1000; ++i)
    {
        SendMessage();
    }

    // 측정 시작
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 실제 작업 수행
    PerformBenchmark();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    
    // 결과 기록
    results.push_back(ComputeMetrics(startTime, endTime));
}

// 평균 및 표준편차 계산
ComputeStatistics(results);
```

#### 하지말아야 할 것 (DON'T)

```cpp
// ❌ 단일 측정만 사용
// 이유: 일회성 이벤트, 캐시 워밍업 등의 영향 제외 불가

// ❌ 측정 중 다른 프로세스 실행
// 이유: CPU/메모리 경합으로 결과 왜곡

// ❌ 지나치게 짧은 테스트 기간 (<10초)
// 이유: 시스템 노이즈로 인한 오차 증가

// ❌ 로깅 코드와 함께 측정
// 이유: I/O 오버헤드로 결과 왜곡
```

---

## Benchmarking Harness

### Architecture

```
┌─────────────────────────────────────┐
│ BenchmarkFramework                  │
├─────────────────────────────────────┤
│                                     │
│ ┌─────────────────────────────────┐ │
│ │ Scenario Manager                │ │
│ │  - Setup environment            │ │
│ │  - Run scenarios                │ │
│ │  - Collect results              │ │
│ └─────────────────────────────────┘ │
│                │                    │
│ ┌──────────────▼──────────────────┐ │
│ │ AsyncIOProvider Tests           │ │
│ │  - IOCP, RIO                    │ │
│ │  - epoll, io_uring              │ │
│ │  - kqueue                       │ │
│ └─────────────────────────────────┘ │
│                                     │
│ ┌─────────────────────────────────┐ │
│ │ Metrics Collector               │ │
│ │  - Timestamp tracking           │ │
│ │  - CPU/Memory sampling          │ │
│ │  - Latency histogram            │ │
│ └─────────────────────────────────┘ │
│                                     │
│ ┌─────────────────────────────────┐ │
│ │ Report Generator                │ │
│ │  - CSV export                   │ │
│ │  - JSON output                  │ │
│ │  - Comparison charts            │ │
│ └─────────────────────────────────┘ │
│                                     │
└─────────────────────────────────────┘
```

### Code Template

```cpp
// 영문: Benchmark harness template
// 한글: 벤치마크 틀 템플릿

#include <Network/AsyncIOProvider.h>
#include <chrono>
#include <vector>
#include <iostream>

// 영문: Base class for all scenarios
// 한글: 모든 시나리오의 기본 클래스
class BenchmarkScenario
{
public:
    virtual ~BenchmarkScenario() = default;

    // 영문: Setup test environment
    // 한글: 테스트 환경 설정
    virtual void Setup() = 0;

    // 영문: Execute benchmark
    // 한글: 벤치마크 실행
    virtual void Execute() = 0;

    // 영문: Cleanup test environment
    // 한글: 테스트 환경 정리
    virtual void Cleanup() = 0;

    // 영문: Get results
    // 한글: 결과 조회
    virtual struct BenchmarkResult GetResult() const = 0;
};

// 영文: Echo server scenario implementation
// 한글: 에코 서버 시나리오 구현
class EchoServerScenario : public BenchmarkScenario
{
private:
    std::unique_ptr<Network::AsyncIOProvider> mProvider;
    std::vector<SOCKET> mClientSockets;
    std::vector<uint64_t> mLatencies;

public:
    void Setup() override
    {
        // 영문: Initialize provider
        // 한글: 공급자 초기화
        mProvider = CreateAsyncIOProvider();
        mProvider->Initialize(1000, 256);

        // 영문: Create client connections
        // 한글: 클라이언트 연결 생성
        // ... (상세 구현)
    }

    void Execute() override
    {
        const int ITERATIONS = 100000;
        const int BATCH_SIZE = 128;

        std::array<Network::CompletionResult, BATCH_SIZE> results;

        for (int i = 0; i < ITERATIONS; ++i)
        {
            // 영문: Post send operations
            // 한글: 송신 작업 게시
            for (auto socket : mClientSockets)
            {
                mProvider->SendAsync(
                    socket,
                    &mTestData,
                    mTestData.size(),
                    [this](const auto& result, void*)
                    {
                        // 영문: Record latency
                        // 한글: 레이턴시 기록
                        mLatencies.push_back(result.mBytesTransferred);
                    }
                );
            }

            // 영문: Process completions
            // 한글: 완료 작업 처리
            uint32_t count = mProvider->ProcessCompletions(
                100,
                results.size(),
                results.data()
            );

            // 영문: Handle results
            // 한글: 결과 처리
            for (uint32_t j = 0; j < count; ++j)
            {
                HandleCompletion(results[j]);
            }
        }
    }

    void Cleanup() override
    {
        // 영문: Close connections and cleanup
        // 한글: 연결 종료 및 정리
        // ... (상세 구현)
    }

    BenchmarkResult GetResult() const override
    {
        // 영문: Calculate metrics
        // 한글: 메트릭 계산
        BenchmarkResult result;
        result.mThroughput = CalculateThroughput();
        result.mAverageLatency = CalculateAverageLatency();
        result.mP99Latency = CalculatePercentile(99);
        return result;
    }
};
```

---

## Expected Results

### Windows Platform

```
╔════════════════════════════════════════════════════════════╗
║ Windows 10 Platform Comparison                             ║
╠════════════════════════════════════════════════════════════╣
║ Metric              │ IOCP        │ RIO                   ║
╟────────────────────────────────────────────────────────────╢
║ Throughput (msg/s)  │ 1.2M        │ 3.6M (3.0x)           ║
║ Latency (avg)       │ 82 μsec     │ 28 μsec (2.9x better) ║
║ Latency (P99)       │ 180 μsec    │ 65 μsec (2.8x better) ║
║ CPU Usage           │ 70%         │ 72% (+2%)             ║
║ Memory              │ 512 MB      │ 520 MB (+8 MB)        ║
╚════════════════════════════════════════════════════════════╝
```

### Linux Platform

```
╔════════════════════════════════════════════════════════════╗
║ Linux Platform Comparison (100 connections)                ║
╠════════════════════════════════════════════════════════════╣
║ Metric              │ epoll       │ io_uring              ║
╟────────────────────────────────────────────────────────────╢
║ Throughput (msg/s)  │ 2.1M        │ 4.8M (2.3x)           ║
║ Latency (avg)       │ 48 μsec     │ 20 μsec (2.4x better) ║
║ Latency (P99)       │ 120 μsec    │ 45 μsec (2.7x better) ║
║ Latency (P99.9)     │ 850 μsec    │ 120 μsec (7.1x!)      ║
║ CPU Usage           │ 68%         │ 70% (+2%)             ║
║ Memory              │ 480 MB      │ 500 MB (+20 MB)       ║
╚════════════════════════════════════════════════════════════╝
```

### Scalability Results

```
연결 수 vs 처리량 (Throughput Scaling)

메시지/초 ▲
    5M  │                            io_uring
        │                          ◆─────◆
    4M  │                     ◆─────    RIO
        │              ◆────────
    3M  │            ◇─────────────    epoll
        │         ◇────
    2M  │      ◇────
        │    ◇────                      IOCP
    1M  │ ◆────◆────◆────◆───────◆
        └─────┼─────┼─────┼─────┼──────┼─
            10    50   100   500  1000

최적: io_uring이 연결 수 증가에 가장 강함
```

---

## Performance Tuning

### Windows Tuning

#### RIO 최적화

```cpp
// 영문: RIO buffer pool pre-allocation
// 한글: RIO 버퍼 풀 사전 할당

// RIO는 버퍼 풀을 최대한 활용하도록 설계
const size_t BUFFER_SIZE = 65536;
const size_t NUM_BUFFERS = 1000;

std::vector<std::vector<char>> bufferPool(NUM_BUFFERS);
for (auto& buf : bufferPool)
{
    buf.resize(BUFFER_SIZE);
    // 영문: Register buffer with RIO
    // 한글: RIO에 버퍼 등록
    provider->SetOption("RegisterBuffer", buf.data(), BUFFER_SIZE);
}
```

#### IOCP 최적화

```cpp
// 영문: IOCP concurrency level tuning
// 한글: IOCP 동시성 수준 튜닝

// 경험적 수치: (CPU 코어 수 - 1) 또는 CPU 코어 수
int concurrencyLevel = std::thread::hardware_concurrency();
provider->SetOption("ConcurrencyLevel", &concurrencyLevel, sizeof(int));
```

### Linux Tuning

#### io_uring 최적화

```cpp
// 영문: io_uring submission queue depth
// 한글: io_uring 제출 큐 깊이

// 기본: 256, 최적: 1024 (메모리 허용 범위)
provider->SetOption("SQDepth", 1024, sizeof(uint32_t));
```

#### epoll 최적화

```cpp
// 영문: Edge-triggered mode (better performance)
// 한글: 엣지 트리거 모드 (더 나은 성능)

provider->SetOption("EdgeTriggered", true, sizeof(bool));
```

---

## Reporting & Analysis

### CSV Format Output

```csv
"Scenario","Backend","Connections","MessageSize","Throughput","AvgLatency","P99Latency","CpuUsage","Memory"
"EchoServer","IOCP","10","1024","1200000","82","180","70.0","512"
"EchoServer","RIO","10","1024","3600000","28","65","72.0","520"
"EchoServer","epoll","10","1024","2100000","48","120","68.0","480"
"EchoServer","io_uring","10","1024","4800000","20","45","70.0","500"
```

### JSON Format Output

```json
{
  "timestamp": "2026-01-27T12:00:00Z",
  "platform": "Windows 10",
  "scenarios": [
    {
      "name": "EchoServer",
      "connections": 100,
      "messageSize": 1024,
      "backends": [
        {
          "name": "IOCP",
          "throughput": 1200000,
          "latency": {
            "average": 82,
            "p99": 180,
            "p999": 420
          },
          "resources": {
            "cpuUsage": 70.0,
            "memory": 512
          }
        }
      ]
    }
  ]
}
```

### Analysis Checklist

```
[ ] 워밍업 영향 확인 (Warmup effect checked)
[ ] 이상값 제거 (Outliers removed)
[ ] 표준편차 확인 (Std deviation < 5%)
[ ] 플랫폼 별 일관성 확인 (Cross-platform consistency)
[ ] 백엔드 비교 분석 (Backend comparison analysis)
[ ] 성능 회귀 분석 (Performance regression analysis)
```

### Recommended Benchmark Environments

#### Environment A: Baseline (Most Common)
```yaml
Platform: Ubuntu 20.04 LTS or Windows 10 Pro
CPU: Intel Core i7 / AMD Ryzen 7 (8-core, 2.5-3.5GHz)
Memory: 16-32GB DDR4
NIC: Intel I219-V or equivalent (1GbE)
Kernel: Linux 5.4 LTS (Ubuntu 20.04) or Windows 10 Build 19041+
Storage: SSD (for logging, not I/O critical)
```

**Usage**: 개발 및 CI/CD 파이프라인, 빠른 회귀 테스트

#### Environment B: Production-like (High-performance)
```yaml
Platform: Ubuntu 22.04 LTS or Windows Server 2022
CPU: Intel Xeon Gold / AMD EPYC (16-32 cores, 2.6-3.0GHz)
Memory: 64-128GB DDR4-3200 or DDR5
NIC: Mellanox ConnectX-5/6 or Intel X710 (10GbE+)
Kernel: Linux 5.15 LTS (latest io_uring optimizations)
Storage: NVMe RAID for disk-intensive tests
```

**Usage**: 성능 검증, 최종 릴리스 전 성능 확인, 경쟁 분석

#### Environment C: Stress Testing (Extreme)
```yaml
Platform: Ubuntu 22.04 LTS / CentOS 8+ or Windows Server 2022
CPU: NUMA system with 64+ cores (AMD EPYC 7002+)
Memory: 256GB+ DDR4-3200 (or DDR5)
NIC: Multiple 100GbE NICs
Kernel: Linux 6.0+ (latest features)
Storage: Ultra-fast NVMe
```

**Usage**: 장시간 스트레스 테스트, 극한 환경 검증, 버그 탐지

### Measurement Conditions Detail

#### Network Configuration
```
loopback (127.0.0.1):
- 가장 빠르고 결정적 (network latency 제거)
- TCP/IP stack 처리량 측정에 적합
- 단점: 실제 네트워크 특성 반영 안 함

LAN (192.168.x.x or 10.0.0.0/8):
- 실제 환경 시뮬레이션
- Latency: ~1-5 ms
- Bandwidth: 1GbE (125MB/s) 또는 10GbE (1250MB/s)
- 추천: 동일 LAN 세그먼트, 직접 연결 (switch 거쳐서 1홉)
```

#### CPU Affinity and Thread Binding
```cpp
// 영문: Pin threads to specific CPU cores
// 한글: 스레드를 특정 CPU 코어에 고정

#include <sched.h>

void PinThreadToCore(int core_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
}

// 사용: AsyncIOProvider 스레드와 테스트 클라이언트를 다른 NUMA 노드에 배치
// NUMA 시스템에서 로컬 메모리 접근 최대화
```

#### Warm-up Phase
```
Rationale:
- 초기 JIT compilation (if applicable)
- CPU cache warmth
- OS 스케줄링 안정화

Procedure:
1. 테스트 시작 전 10-20초 동안 초기 요청 전송 (무시)
2. 측정 시작
3. 최소 60초 이상 지속 (통계적 유의성)
4. 추가 10초 쿨다운 (CPU 온도 정상화)

Code Example:
```cpp
// Warmup
for (int i = 0; i < 100000; i++) {
    provider->SendAsync(...);
    if (i % 10000 == 0) provider->FlushRequests();
}
// Allow 1-2 seconds for processing
std::this_thread::sleep_for(std::chrono::seconds(2));

// Measure (60 seconds)
auto start = std::chrono::high_resolution_clock::now();
// ... benchmark operations ...
auto end = std::chrono::high_resolution_clock::now();
```

#### Statistical Rigor
```
Sample Size:
- 최소 1M 이상의 샘플 (통계적 유의성)
- 권장: 10M+ 샘플 (표준편차 <1%)

Outlier Handling (Grubbs Test):
- Z-score > 3.0 제거 (극단값)
- 하지만 너무 많이 제거하지 않음 (<1% 제거)

Reporting:
- Mean (평균): 주 메트릭
- Median (중앙값): 이상값 영향 무시
- P50, P95, P99, P99.9: 꼬리 레이턴시 분석
- Std Dev: 일관성 평가
- Min/Max: 범위 파악
```

### Cross-Platform Measurement Protocol

#### Phase 1: Baseline Establishment
```
Week 1: IOCP only (Windows) / epoll only (Linux)
- 현재 상태 기준선 확인
- 플랫폼별 특성 파악
- 자동화 스크립트 검증
```

#### Phase 2: Single Backend Testing
```
Week 2: RIO (Windows) only
- RIO AsyncIOProvider 성능 측정
- IOCP와 비교
- 예상 3x 달성 확인
```

```
Week 3: io_uring (Linux) only
- io_uring AsyncIOProvider 성능 측정
- epoll과 비교
- 예상 4x 달성 확인
```

#### Phase 3: Option Comparison
```
Week 4: All Options (Option A, B, C if applicable)
- 각 옵션별 성능 비교
- 마이그레이션 비용 분석
- 최종 권장 옵션 선정
```

#### Phase 4: Continuous Benchmarking
```
Per Release: 성능 회귀 테스트
- 변경사항 전후 성능 비교
- 5% 이상 하락 시 분석
- 성능 히스토리 유지
```

---

## Troubleshooting

### 문제: 결과가 예상과 다름 (Results differ from expectations)

**원인 확인**:
1. 시스템 리소스 경합 → `htop`, `perfmon` 확인
2. 네트워크 버퍼 부족 → 버퍼 크기 증가
3. 드라이버 이슈 → 최신 버전 업데이트

### 문제: io_uring이 epoll보다 느림

**원인**:
- 커널 버전 낮음 (5.1 대신 최신 권장)
- CQ 깊이 너무 작음
- 버퍼 풀 미등록

---

**다음 단계**: 첫 번째 벤치마크 실행 및 기준선 설정
**목표 완료일**: 2026년 2월 15일

---

---

# 실제 측정 결과 기록

## 측정 환경

- **플랫폼**: Windows 11 Pro (x64)
- **빌드**: x64 Release (MSVC v143)
- **백엔드**: RIO (auto)
- **도구**: `run_perf_test.ps1 -Phase all -SustainSec 30 -BinMode Release`

---

## 2026-02-26 (Debug, Baseline)

| 항목 | 값 |
|------|----|
| 빌드 | x64 Debug |
| 시나리오 | Baseline (1 client, 10s) |
| 실제 연결 | 1 / 1 |
| RTT min/avg/max | 0ms / 2ms / 5ms |
| 판정 | **PASS** |

---

## 2026-02-27 (Debug, Ramp-up)

| 단계 | 실제 연결 | 오류 | Server WS | 판정 |
|------|-----------|------|-----------|------|
| 10   | 10        | 0    | 35.1 MB   | **PASS** |
| 50   | 50        | 0    | 35.9 MB   | **PASS** |
| 100  | 100       | 0    | 37.0 MB   | **PASS** |
| 500  | 500       | 0    | 44.9 MB   | **PASS** |
| 1000 | 655       | 4425 | N/A       | **FAIL** (WSA 10055) |

> Debug 환경 한계: 최대 안정 동접 500

---

## 2026-02-28 (Release, RIO slab pool 도입 후)

### Phase 2 — Ramp-up (x64 Release, 30s/단계)

| 단계 | 실제 연결 | 오류 | Server WS | RTT avg | 판정 |
|------|-----------|------|-----------|---------|------|
| 10   | 10        | 0    | 22.1 MB   | 1 ms    | **PASS** |
| 100  | 100       | 0    | 23.8 MB   | 0 ms    | **PASS** |
| 500  | 500       | 0    | 31.2 MB   | 0 ms    | **PASS** |
| 1000 | 564       | 2607 | 31.7 MB   | 0 ms    | **FAIL** (WSA 10055) |

### Debug vs Release 비교 (500 클라이언트 기준)

| 항목 | Debug (02-27) | Release (02-28) | 변화 |
|------|--------------|-----------------|------|
| Server WS | 44.9 MB | 31.2 MB | **-30.5%** |
| Server Handles | 662 | 636 | -3.9% |

---

## 2026-02-28 (Release, slab 2차 실행)

전체 Phase 실행 (`-RampClients @(10,100,500,1000) -SustainSec 30`):

| 단계 | 실제 연결 | 오류 | Server WS | RTT | 판정 |
|------|-----------|------|-----------|-----|------|
| 10   | 10        | 0    | 179 MB    | avg=1ms | **PASS** |
| 100  | 100       | 0    | 173.2 MB  | avg=1ms | **PASS** |
| 500  | 500       | 0    | 188.2 MB  | avg=0ms | **PASS** |
| 1000 | 1000      | 0    | 193.8 MB  | avg=0ms | **PASS** |

> **1000/1000 최초 PASS** 달성

---

## 2026-03-01 (Release, 메모리 풀 3단계 최적화 후)

**최적화 내용**: AsyncBufferPool O(1), ProcessRawRecv 배치 버퍼, SendBufferPool zero-copy

### Phase 0 — Smoke Test

| 항목 | 값 |
|------|----|
| 결과 | **PASS** |
| RTT min/avg/max | 0ms / 3ms / 6ms |
| Server WS / Handles / Threads | 178.8 MB / 137 / 28 |

### Phase 1 — 안정성

| 항목 | 값 |
|------|----|
| Graceful Shutdown (2 clients, 30s) | **PASS** |
| WAL 상태 | Clean |

### Phase 2 — Ramp-up (x64 Release, 30s/단계)

| 단계 | 목표 | 실제 | 오류 | Server WS | RTT avg | 판정 |
|------|------|------|------|-----------|---------|------|
| 10   | 10   | 10   | 0    | 178.9 MB  | 1 ms    | **PASS** |
| 100  | 100  | 100  | 0    | 180.6 MB  | 1 ms    | **PASS** |
| 500  | 500  | 500  | 0    | 188 MB    | 0 ms    | **PASS** |
| 1000 | 1000 | 1000 | 0    | 193.7 MB  | 0 ms    | **PASS** |

> 상세 로그: `Doc/Performance/Logs/20260301_111832/`

---

## 누적 성능 추이

| 날짜 | 빌드 | 최대 안정 동접 | 1000 판정 | 비고 |
|------|------|---------------|-----------|------|
| 2026-02-27 | Debug | 500 | FAIL (WSA 10055) | 초기 측정 |
| 2026-02-28 (1차) | Release | 500 | FAIL (WSA 10055) | Release 첫 측정 |
| 2026-02-28 (2차) | Release | 1000 | **PASS** | RIO slab pool 도입 |
| 2026-03-01 | Release | 1000 | **PASS** | 메모리 풀 3단계 최적화 |

