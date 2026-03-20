CREATE TABLE IF NOT EXISTS T_SessionConnectLog (
    id           BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    session_id   BIGINT NOT NULL,
    connect_time VARCHAR(32) NOT NULL
);
