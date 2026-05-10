IF OBJECT_ID(N'dbo.T_SessionConnectLog', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_SessionConnectLog (
        id           BIGINT IDENTITY(1,1) NOT NULL PRIMARY KEY,
        session_id   BIGINT NOT NULL,
        connect_time DATETIME2 NOT NULL
    );
END;
