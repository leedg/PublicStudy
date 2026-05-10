IF OBJECT_ID(N'dbo.T_PlayerData', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_PlayerData (
        session_id BIGINT NOT NULL PRIMARY KEY,
        data       NVARCHAR(MAX) NULL
    );
END;
