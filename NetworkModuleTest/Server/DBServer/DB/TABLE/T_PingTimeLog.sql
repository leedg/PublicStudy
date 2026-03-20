CREATE TABLE IF NOT EXISTS T_PingTimeLog (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id   INTEGER NOT NULL,
    server_name TEXT NOT NULL,
    ping_time   TEXT NOT NULL
);
