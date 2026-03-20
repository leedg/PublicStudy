MERGE INTO T_PlayerData AS target
USING (VALUES (?, ?)) AS source (session_id, data)
ON target.session_id = source.session_id
WHEN MATCHED THEN
    UPDATE SET data = source.data
WHEN NOT MATCHED THEN
    INSERT (session_id, data)
    VALUES (source.session_id, source.data);
