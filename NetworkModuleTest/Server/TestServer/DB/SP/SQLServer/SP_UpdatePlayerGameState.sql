MERGE INTO T_GameStates AS target
USING (VALUES (?, ?)) AS source (user_id, state_data)
ON target.user_id = source.user_id
WHEN MATCHED THEN
    UPDATE SET state_data = source.state_data,
               updated_at = CURRENT_TIMESTAMP
WHEN NOT MATCHED THEN
    INSERT (user_id, state_data, updated_at)
    VALUES (source.user_id, source.state_data, CURRENT_TIMESTAMP);
