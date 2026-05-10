INSERT INTO T_GameStates (
    user_id,
    state_data,
    updated_at
) VALUES (
    ?,
    ?,
    CURRENT_TIMESTAMP
)
ON CONFLICT(user_id) DO UPDATE SET
    state_data = excluded.state_data,
    updated_at = CURRENT_TIMESTAMP;
