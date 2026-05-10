CREATE TABLE IF NOT EXISTS T_UserLogins (
    id         BIGSERIAL PRIMARY KEY,
    user_id    BIGINT NOT NULL,
    username   VARCHAR(255) NOT NULL,
    login_time TIMESTAMP NOT NULL
);
