INSERT INTO T_GameStates (
    user_id,
    state_data,
    updated_at
) VALUES (
    ?,
    ?,
    CURRENT_TIMESTAMP
)
ON DUPLICATE KEY UPDATE
    state_data = VALUES(state_data),
    updated_at = CURRENT_TIMESTAMP;
