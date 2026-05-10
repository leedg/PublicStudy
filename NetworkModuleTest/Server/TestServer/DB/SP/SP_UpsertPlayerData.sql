INSERT INTO T_PlayerData (
    session_id,
    data
) VALUES (
    ?,
    ?
)
ON CONFLICT(session_id) DO UPDATE SET
    data = excluded.data;
