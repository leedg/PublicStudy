IF OBJECT_ID(N'dbo.T_ServerLatencyLog', N'U') IS NULL
BEGIN
    CREATE TABLE dbo.T_ServerLatencyLog (
        id            BIGINT IDENTITY(1,1) NOT NULL PRIMARY KEY,
        server_id     BIGINT NOT NULL,
        server_name   NVARCHAR(255) NOT NULL,
        rtt_ms        BIGINT NOT NULL,
        avg_rtt_ms    FLOAT NULL,
        min_rtt_ms    BIGINT NULL,
        max_rtt_ms    BIGINT NULL,
        ping_count    BIGINT NULL,
        measured_time NVARCHAR(32) NOT NULL
    );
END;
