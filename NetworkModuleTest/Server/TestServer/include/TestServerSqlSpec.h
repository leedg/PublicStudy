#pragma once

#include "../../ServerEngine/Database/SqlModuleBootstrap.h"

namespace Network::TestServer
{

inline const Network::Database::SqlModuleBootstrap::ModuleSpec&
GetTestServerSqlModuleSpec()
{
    using Network::Database::SqlModuleBootstrap::ModuleSpec;

    static const ModuleSpec spec = [] {
        ModuleSpec value;
        value.moduleName = "TestServer";
        value.tableScripts = {
            "TABLE/T_SessionConnectLog.sql",
            "TABLE/T_SessionDisconnectLog.sql",
            "TABLE/T_PlayerData.sql",
            "TABLE/T_UserLogins.sql",
            "TABLE/T_Users.sql",
            "TABLE/T_GameStates.sql"
        };
        value.managedScripts = value.tableScripts;
        value.managedScripts.insert(
            value.managedScripts.end(),
            {
                "SP/SP_InsertSessionConnectLog.sql",
                "SP/SP_InsertSessionDisconnectLog.sql",
                "SP/SP_UpsertPlayerData.sql",
                "SP/SP_InsertUserLoginEvent.sql",
                "SP/SP_UpsertUserProfile.sql",
                "SP/SP_SelectUserProfile.sql",
                "SP/SP_UpdatePlayerGameState.sql",
                "RAW/RQ_UpdateUserProfileName.sql"
            });
        return value;
    }();

    return spec;
}

} // namespace Network::TestServer
