CREATE TABLE IF NOT EXISTS T_GameStates (
    user_id    BIGINT PRIMARY KEY,
    state_data TEXT NULL,
    updated_at TIMESTAMP NOT NULL
);
