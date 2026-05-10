MERGE INTO T_Users AS target
USING (VALUES (?, ?)) AS source (user_id, username)
ON target.user_id = source.user_id
WHEN MATCHED THEN
    UPDATE SET username = source.username
WHEN NOT MATCHED THEN
    INSERT (user_id, username, created_at)
    VALUES (source.user_id, source.username, CURRENT_TIMESTAMP);
