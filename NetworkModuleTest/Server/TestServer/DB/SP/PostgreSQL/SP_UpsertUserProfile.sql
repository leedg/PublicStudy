INSERT INTO T_Users (
    user_id,
    username,
    created_at
) VALUES (
    ?,
    ?,
    CURRENT_TIMESTAMP
)
ON CONFLICT(user_id) DO UPDATE SET
    username = excluded.username;
