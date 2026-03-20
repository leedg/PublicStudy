IF OBJECT_ID(N'dbo.T_UserLogins', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_UserLogins (
        id         BIGINT IDENTITY(1,1) NOT NULL PRIMARY KEY,
        user_id    BIGINT NOT NULL,
        username   NVARCHAR(255) NOT NULL,
        login_time DATETIME2 NOT NULL
    );
END;
