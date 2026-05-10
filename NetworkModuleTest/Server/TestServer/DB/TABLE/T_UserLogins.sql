CREATE TABLE IF NOT EXISTS T_UserLogins (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL,
    username   TEXT NOT NULL,
    login_time TEXT NOT NULL
);
