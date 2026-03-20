CREATE TABLE IF NOT EXISTS T_PingTimeLog (
    id          BIGSERIAL PRIMARY KEY,
    server_id   BIGINT NOT NULL,
    server_name VARCHAR(255) NOT NULL,
    ping_time   VARCHAR(32) NOT NULL
);
