IF OBJECT_ID(N'dbo.T_PingTimeLog', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_PingTimeLog (
        id          BIGINT IDENTITY(1,1) NOT NULL PRIMARY KEY,
        server_id   BIGINT NOT NULL,
        server_name NVARCHAR(255) NOT NULL,
        ping_time   NVARCHAR(32) NOT NULL
    );
END;
