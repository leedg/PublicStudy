CREATE TABLE IF NOT EXISTS T_ServerLatencyLog (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id     INTEGER NOT NULL,
    server_name   TEXT NOT NULL,
    rtt_ms        INTEGER NOT NULL,
    avg_rtt_ms    REAL,
    min_rtt_ms    INTEGER,
    max_rtt_ms    INTEGER,
    ping_count    INTEGER,
    measured_time TEXT NOT NULL
);
