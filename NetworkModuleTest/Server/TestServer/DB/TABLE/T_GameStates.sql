CREATE TABLE IF NOT EXISTS T_GameStates (
    user_id    INTEGER PRIMARY KEY,
    state_data TEXT,
    updated_at TEXT NOT NULL
);
