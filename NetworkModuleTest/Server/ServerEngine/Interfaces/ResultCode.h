#pragma once

// Shared result code enum — used by both client responses and server-server communication.
// 한글: 공용 결과 코드 enum — 클라이언트 응답과 서버간 통신 모두 사용.

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
        Unknown                 = 1,
        InvalidRequest          = 2,
        Timeout                 = 3,
        NotInitialized          = 4,
        ShuttingDown            = 5,

        // ── 세션 / 인증 오류 (1000~1999) ──────────────────────────────────────
        SessionNotFound         = 1000,
        SessionExpired          = 1001,
        NotAuthenticated        = 1002,
        PermissionDenied        = 1003,
        DuplicateSession        = 1004,

        // ── DB 오류 (2000~2999) ───────────────────────────────────────────────
        DBConnectionFailed      = 2000,
        DBQueryFailed           = 2001,
        DBRecordNotFound        = 2002,
        DBDuplicateKey          = 2003,
        DBTransactionFailed     = 2004,

        // ── 서버간 통신 오류 (3000~3999) ──────────────────────────────────────
        DBServerNotConnected    = 3000,
        DBServerTimeout         = 3001,
        DBServerRejected        = 3002,

        // ── 게임 로직 오류 (4000~4999) ────────────────────────────────────────
        InsufficientResources   = 4000,
        InvalidGameState        = 4001,
        ItemNotFound            = 4002,
        LevelRequirementNotMet  = 4003,

        Max
    };

    inline bool IsSuccess(ResultCode code) { return code == ResultCode::Success; }

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
