IF OBJECT_ID(N'dbo.T_GameStates', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_GameStates (
        user_id    BIGINT NOT NULL PRIMARY KEY,
        state_data NVARCHAR(MAX) NULL,
        updated_at DATETIME2 NOT NULL
    );
END;
