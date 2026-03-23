#pragma once

// 클라이언트 응답 및 서버 간 통신에 공통으로 사용되는 결과 코드.
// int32_t 기반으로 네트워크 직렬화와 확장이 용이하다.
// 범위별 의미:
//   0         — 성공
//   1~999     — 공통 오류 (대부분 복구 불가 / 재시도 여부는 호출자 판단)
//   1000~1999 — 세션/인증 오류 (로그인 재시도 등으로 복구 가능)
//   2000~2999 — DB 오류 (DBConnectionFailed는 재접속으로 복구 가능,
//                         나머지는 쿼리/데이터 수준 문제)
//   3000~3999 — 서버 간 통신 오류 (DBServer 재접속으로 복구 가능)
//   4000~4999 — 게임 로직 오류 (클라이언트에 그대로 전달)

#include <cstdint>

namespace Network
{
    // =========================================================================
    // ResultCode — int32_t 기반으로 확장 가능한 결과 코드
    // =========================================================================

    enum class ResultCode : int32_t
    {
        // ── 성공 ──────────────────────────────────────────────────────────────
        Success                 = 0,

        // ── 공통 오류 (1~999) ─────────────────────────────────────────────────
        Unknown                 = 1,    // 원인 불명 — 서버 로그 확인 필요
        InvalidRequest          = 2,    // 잘못된 요청 형식 — 복구 불가
        Timeout                 = 3,    // 처리 시간 초과 — 재시도 가능
        NotInitialized          = 4,    // 서버 모듈 초기화 미완료
        ShuttingDown            = 5,    // 서버 종료 중 — 재접속 필요

        // ── 세션 / 인증 오류 (1000~1999) ──────────────────────────────────────
        SessionNotFound         = 1000, // 세션이 존재하지 않음 — 재로그인 필요
        SessionExpired          = 1001, // 세션 만료 — 재로그인 필요
        NotAuthenticated        = 1002, // 인증 미완료 — 로그인 선행 필요
        PermissionDenied        = 1003, // 권한 부족 — 복구 불가
        DuplicateSession        = 1004, // 중복 로그인 감지

        // ── DB 오류 (2000~2999) ───────────────────────────────────────────────
        DBConnectionFailed      = 2000, // DB 연결 실패 — 재접속 후 복구 가능
        DBQueryFailed           = 2001, // 쿼리 실행 실패 — 서버 로그 확인
        DBRecordNotFound        = 2002, // 대상 레코드 없음 — 복구 불가
        DBDuplicateKey          = 2003, // 고유키 충돌 — 입력값 수정 필요
        DBTransactionFailed     = 2004, // 트랜잭션 실패 — 롤백됨, 재시도 가능

        // ── 서버간 통신 오류 (3000~3999) ──────────────────────────────────────
        DBServerNotConnected    = 3000, // DBServer 연결 없음 — 재접속 대기
        DBServerTimeout         = 3001, // DBServer 응답 없음 — 재시도 가능
        DBServerRejected        = 3002, // DBServer가 요청 거부

        // ── 게임 로직 오류 (4000~4999) ────────────────────────────────────────
        InsufficientResources   = 4000, // 재화/아이템 부족 — 클라이언트에 전달
        InvalidGameState        = 4001, // 유효하지 않은 게임 상태 전환
        ItemNotFound            = 4002, // 아이템 없음
        LevelRequirementNotMet  = 4003, // 레벨 조건 미충족

        Max
    };

    // 성공 여부 빠른 확인
    inline bool IsSuccess(ResultCode code) { return code == ResultCode::Success; }

    // 로깅/디버그용 문자열 변환
    inline const char* ToString(ResultCode code)
    {
        switch (code)
        {
        case ResultCode::Success:               return "Success";
        case ResultCode::Unknown:               return "Unknown";
        case ResultCode::InvalidRequest:        return "InvalidRequest";
        case ResultCode::Timeout:               return "Timeout";
        case ResultCode::NotInitialized:        return "NotInitialized";
        case ResultCode::ShuttingDown:          return "ShuttingDown";
        case ResultCode::SessionNotFound:       return "SessionNotFound";
        case ResultCode::SessionExpired:        return "SessionExpired";
        case ResultCode::NotAuthenticated:      return "NotAuthenticated";
        case ResultCode::PermissionDenied:      return "PermissionDenied";
        case ResultCode::DuplicateSession:      return "DuplicateSession";
        case ResultCode::DBConnectionFailed:    return "DBConnectionFailed";
        case ResultCode::DBQueryFailed:         return "DBQueryFailed";
        case ResultCode::DBRecordNotFound:      return "DBRecordNotFound";
        case ResultCode::DBDuplicateKey:        return "DBDuplicateKey";
        case ResultCode::DBTransactionFailed:   return "DBTransactionFailed";
        case ResultCode::DBServerNotConnected:  return "DBServerNotConnected";
        case ResultCode::DBServerTimeout:       return "DBServerTimeout";
        case ResultCode::DBServerRejected:      return "DBServerRejected";
        case ResultCode::InsufficientResources: return "InsufficientResources";
        case ResultCode::InvalidGameState:      return "InvalidGameState";
        case ResultCode::ItemNotFound:          return "ItemNotFound";
        case ResultCode::LevelRequirementNotMet:return "LevelRequirementNotMet";
        default:                                return "Unknown";
        }
    }

} // namespace Network
