CREATE TABLE IF NOT EXISTS T_SessionConnectLog (
    id           BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    session_id   BIGINT NOT NULL,
    connect_time DATETIME NOT NULL
);
