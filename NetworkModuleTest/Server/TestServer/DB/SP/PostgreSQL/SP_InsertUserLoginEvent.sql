INSERT INTO T_UserLogins (
    user_id,
    username,
    login_time
) VALUES (
    ?,
    ?,
    CURRENT_TIMESTAMP
);
