CREATE TABLE IF NOT EXISTS T_SessionDisconnectLog (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      INTEGER NOT NULL,
    disconnect_time TEXT NOT NULL
);
