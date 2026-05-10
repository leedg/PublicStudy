INSERT INTO T_ServerLatencyLog (
    server_id,
    server_name,
    rtt_ms,
    avg_rtt_ms,
    min_rtt_ms,
    max_rtt_ms,
    ping_count,
    measured_time
) VALUES (
    ?,
    ?,
    ?,
    ?,
    ?,
    ?,
    ?,
    ?
);
