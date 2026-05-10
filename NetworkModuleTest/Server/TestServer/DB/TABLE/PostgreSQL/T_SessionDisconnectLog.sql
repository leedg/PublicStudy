CREATE TABLE IF NOT EXISTS T_SessionDisconnectLog (
    id              BIGSERIAL PRIMARY KEY,
    session_id      BIGINT NOT NULL,
    disconnect_time TIMESTAMP NOT NULL
);
