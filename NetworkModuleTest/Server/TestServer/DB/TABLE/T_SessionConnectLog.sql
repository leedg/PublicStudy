CREATE TABLE IF NOT EXISTS T_SessionConnectLog (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id   INTEGER NOT NULL,
    connect_time TEXT NOT NULL
);
