IF OBJECT_ID(N'dbo.T_SessionDisconnectLog', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_SessionDisconnectLog (
        id              BIGINT IDENTITY(1,1) NOT NULL PRIMARY KEY,
        session_id      BIGINT NOT NULL,
        disconnect_time DATETIME2 NOT NULL
    );
END;
