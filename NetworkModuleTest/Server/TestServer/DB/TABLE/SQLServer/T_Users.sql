IF OBJECT_ID(N'dbo.T_Users', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_Users (
        user_id    BIGINT NOT NULL PRIMARY KEY,
        username   NVARCHAR(255) NOT NULL,
        created_at DATETIME2 NOT NULL
    );
END;
