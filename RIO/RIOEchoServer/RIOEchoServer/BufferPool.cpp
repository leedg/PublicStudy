#include "BufferPool.h"
#include "RIONetwork.h"
#include <cstring>

// ============================================================
// BufferPool 생성자 - SRW Lock 초기화
// 
// Slim Reader-Writer Lock 초기화
// - 경량 동기화 객체 (크리티컬 섹션보다 가벼움)
// - 여러 스레드가 동시에 버퍼를 할당/해제할 수 있도록 보호
// ============================================================
BufferPool::BufferPool()
{
    InitializeSRWLock(&m_lock);
}

// ============================================================
// BufferPool 소멸자 - 리소스 정리
// 
// RAII 패턴: 소멸자에서 자동으로 리소스 정리
// ============================================================
BufferPool::~BufferPool()
{
    cleanup();
}

// ============================================================
// 버퍼 풀 초기화
//
// 대형 메모리 블록을 할당하고 이를 고정 크기 슬라이스로 분할합니다.
// RIO API가 Zero-Copy I/O를 수행할 수 있도록 버퍼를 등록합니다.
//
// 매개변수:
//   - totalBytes: 전체 버퍼 크기 (바이트)
//   - sliceSize: 각 슬라이스(청크)의 크기 (바이트)
//
// 동작 과정:
// 1. VirtualAlloc으로 대형 메모리 블록 할당 (커널 + 유저)
// 2. RIORegisterBuffer로 RIO API에 버퍼 등록
// 3. 슬라이스 개수 계산 및 사용 여부 추적 배열 초기화
//
// 왜 VirtualAlloc을 사용하는가?
// - malloc보다 큰 메모리 블록에 효율적
// - 페이지 경계 정렬 보장 (성능 향상)
// - RIO API가 요구하는 메모리 정렬 조건 충족
//
// 반환값: 초기화 성공 시 true, 실패 시 false
// ============================================================
bool BufferPool::init(uint64_t totalBytes, uint32_t sliceSize)
{
    // 이미 할당된 리소스가 있다면 먼저 정리
    cleanup();

    // sliceSize로 나누어떨어지도록 전체 크기 조정
    // 예: totalBytes=1000, sliceSize=64 -> m_total=960 (15개 슬라이스)
    m_total = (uint64_t)((totalBytes / sliceSize) * sliceSize);
    m_sliceSize = sliceSize;

    // 유효성 검사: 크기가 0이면 초기화 실패
    if (m_total == 0 || m_sliceSize == 0)
    {
        return false;
    }

    // VirtualAlloc으로 메모리 할당 (커밋 + 예약)
    // - MEM_COMMIT: 물리 메모리 즉시 할당
    // - MEM_RESERVE: 가상 주소 공간 예약
    // - PAGE_READWRITE: 읽기/쓰기 권한 부여
    m_base = (char*)VirtualAlloc(NULL, (SIZE_T)m_total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!m_base)
    {
        return false;  // 메모리 할당 실패
    }

    // RIO 버퍼로 등록 (Registered I/O를 위한 필수 단계)
    // 이 단계를 거쳐야 RIO API가 이 버퍼를 사용할 수 있음
    // 
    // 등록의 장점:
    // - 커널이 버퍼 주소를 미리 알고 있어 I/O 시 빠름
    // - Zero-Copy: 커널과 유저 메모리 사이 복사 불필요
    // - DMA(Direct Memory Access) 가능
    m_bid = RIONetwork::Rio().RIORegisterBuffer(m_base, (DWORD)m_total);

    if (m_bid == RIO_INVALID_BUFFERID)
    {
        return false;  // RIO 등록 실패
    }

    // 슬라이스 개수 계산
    // 예: 1MB 버퍼, 4KB 슬라이스 -> 256개 슬라이스
    m_slices = (uint32_t)(m_total / m_sliceSize);

    // 모든 슬라이스 사용 여부 추적 배열 초기화
    // 0 = 사용 가능, 1 = 사용 중
    // 처음엔 모두 사용 가능 상태(0)로 초기화
    m_used.assign(m_slices, 0);

    return true;
}

// ============================================================
// 버퍼 풀 정리 및 리소스 해제
//
// 역순으로 정리:
// 1. RIO 버퍼 등록 해제
// 2. 메모리 해제
// 3. 내부 상태 초기화
// ============================================================
void BufferPool::cleanup()
{
    // RIO 버퍼 등록 해제
    // 이후에는 이 버퍼로 RIO I/O 불가
    if (m_bid != RIO_INVALID_BUFFERID)
    {
        RIONetwork::Rio().RIODeregisterBuffer(m_bid);
        m_bid = RIO_INVALID_BUFFERID;
    }

    // 메모리 해제
    // VirtualFree: VirtualAlloc으로 할당한 메모리 해제
    if (m_base)
    {
        VirtualFree(m_base, 0, MEM_RELEASE);
        m_base = nullptr;
    }

    // 내부 배열 및 메타데이터 초기화
    m_used.clear();
    m_total = 0;
    m_sliceSize = 0;
    m_slices = 0;
}

// ============================================================
// 슬라이스 할당
//
// 버퍼 풀에서 사용 가능한 슬라이스를 찾아 할당합니다.
//
// 매개변수:
//   - offsetOut: 할당된 슬라이스의 오프셋 (출력 매개변수)
//                할당 성공 시 이 값으로 버퍼 주소를 계산: base + offset
//
// 반환값: 할당 성공 시 true, 실패 시 false (풀이 가득 참)
//
// 동작 순서:
// 1. SRW Lock으로 배타적 액세스 획득 (다른 스레드 대기)
// 2. 선형 탐색으로 첫 번째 빈 슬라이스 찾기
// 3. 해당 슬라이스를 "사용 중(1)"으로 표시
// 4. 슬라이스의 오프셋을 계산하여 반환
// 5. Lock 해제
//
// 참고: O(n) 시간 복잡도이지만 실제로는 빠름
//       (대부분의 경우 앞쪽에서 빈 슬라이스 발견)
// ============================================================
bool BufferPool::allocSlice(uint32_t& offsetOut)
{
    // 배타적 Lock 획득 (쓰기 작업)
    AcquireSRWLockExclusive(&m_lock);

    // 빈 슬라이스 검색 (선형 탐색)
    for (uint32_t i = 0; i < m_slices; ++i)
    {
        if (!m_used[i])  // 사용 가능한 슬라이스 발견
        {
            // 슬라이스를 "사용 중"으로 표시
            m_used[i] = 1;

            // 슬라이스의 오프셋 계산
            // 예: i=5, sliceSize=4096 -> offset=20480
            offsetOut = i * m_sliceSize;

            // Lock 해제
            ReleaseSRWLockExclusive(&m_lock);

            return true;  // 할당 성공
        }
    }

    // 모든 슬라이스가 사용 중 - Lock 해제 후 실패 반환
    ReleaseSRWLockExclusive(&m_lock);

    return false;  // 할당 실패 (풀이 가득 찼음)
}

// ============================================================
// 슬라이스 해제
//
// 사용이 끝난 슬라이스를 버퍼 풀에 반환합니다.
//
// 매개변수:
//   - offset: 해제할 슬라이스의 오프셋
//             (allocSlice에서 받았던 offsetOut 값)
//
// 동작 순서:
// 1. 오프셋으로부터 슬라이스 인덱스 계산
// 2. 유효성 검사 (범위 초과 시 무시)
// 3. SRW Lock 획득
// 4. 해당 슬라이스를 "사용 가능(0)"으로 표시
// 5. Lock 해제
//
// 참고: 
// - 이중 해제 방지를 위한 추가 검증은 없음 (성능을 위해)
// - 잘못된 offset 전달 시 다른 슬라이스가 해제될 수 있으므로 주의
// ============================================================
void BufferPool::freeSlice(uint32_t offset)
{
    // 오프셋으로부터 슬라이스 인덱스 계산
    // 예: offset=20480, sliceSize=4096 -> idx=5
    uint32_t idx = offset / m_sliceSize;

    // 유효성 검사: 인덱스가 범위를 벗어나면 무시
    // (잘못된 offset이 전달된 경우)
    if (idx >= m_slices)
    {
        return;
    }

    // 배타적 Lock 획득
    AcquireSRWLockExclusive(&m_lock);

    // 슬라이스를 "사용 가능"으로 표시
    // 이제 다른 allocSlice 호출에서 이 슬라이스를 재사용 가능
    m_used[idx] = 0;

    // Lock 해제
    ReleaseSRWLockExclusive(&m_lock);
}
