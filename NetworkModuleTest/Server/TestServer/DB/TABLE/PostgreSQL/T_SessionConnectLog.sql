CREATE TABLE IF NOT EXISTS T_SessionConnectLog (
    id           BIGSERIAL PRIMARY KEY,
    session_id   BIGINT NOT NULL,
    connect_time TIMESTAMP NOT NULL
);
