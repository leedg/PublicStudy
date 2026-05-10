CREATE TABLE IF NOT EXISTS T_ServerLatencyLog (
    id            BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    server_id     BIGINT NOT NULL,
    server_name   VARCHAR(255) NOT NULL,
    rtt_ms        BIGINT NOT NULL,
    avg_rtt_ms    DOUBLE NULL,
    min_rtt_ms    BIGINT NULL,
    max_rtt_ms    BIGINT NULL,
    ping_count    BIGINT NULL,
    measured_time VARCHAR(32) NOT NULL
);
