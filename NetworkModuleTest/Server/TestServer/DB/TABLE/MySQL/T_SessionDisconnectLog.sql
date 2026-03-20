CREATE TABLE IF NOT EXISTS T_SessionDisconnectLog (
    id              BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    session_id      BIGINT NOT NULL,
    disconnect_time DATETIME NOT NULL
);
