#pragma once

// English: Database factory for creating database instances
// 한글: 데이터베이스 인스턴스 생성용 팩토리

#include "../Interfaces/IDatabase.h"
#include "../Interfaces/DatabaseType_enum.h"
#include <memory>

namespace Network {
namespace Database {

    // =============================================================================
    // English: DatabaseFactory class
    // 한글: DatabaseFactory 클래스
    // =============================================================================

    /**
     * English: Database factory for creating database instances
     * 한글: 데이터베이스 인스턴스 생성용 팩토리
     */
    class DatabaseFactory 
    {
    public:
        // English: Create database by type
        // 한글: 타입별 데이터베이스 생성
        static std::unique_ptr<IDatabase> CreateDatabase(DatabaseType type);

        // English: Convenience methods
        // 한글: 편의 메서드
        static std::unique_ptr<IDatabase> CreateODBCDatabase();
        static std::unique_ptr<IDatabase> CreateOLEDBDatabase();
    };

}  // namespace Database
}  // namespace Network
