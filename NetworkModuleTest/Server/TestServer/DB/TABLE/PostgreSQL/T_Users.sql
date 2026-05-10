CREATE TABLE IF NOT EXISTS T_Users (
    user_id    BIGINT PRIMARY KEY,
    username   VARCHAR(255) NOT NULL,
    created_at TIMESTAMP NOT NULL
);
