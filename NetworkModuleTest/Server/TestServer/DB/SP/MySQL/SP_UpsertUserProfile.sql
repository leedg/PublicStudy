INSERT INTO T_Users (
    user_id,
    username,
    created_at
) VALUES (
    ?,
    ?,
    CURRENT_TIMESTAMP
)
ON DUPLICATE KEY UPDATE
    username = VALUES(username);
