CREATE TABLE IF NOT EXISTS T_GameStates (
    user_id    BIGINT NOT NULL PRIMARY KEY,
    state_data TEXT NULL,
    updated_at DATETIME NOT NULL
);
