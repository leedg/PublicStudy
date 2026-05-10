INSERT INTO T_PlayerData (
    session_id,
    data
) VALUES (
    ?,
    ?
)
ON DUPLICATE KEY UPDATE
    data = VALUES(data);
