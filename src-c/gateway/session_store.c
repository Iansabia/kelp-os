#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include "session_store.h"
#include "openclaw.h"

struct session_store {
    sqlite3 *db;
};

static int ensure_tables(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id TEXT PRIMARY KEY,"
        "  channel_id TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT NOT NULL,"
        "  role TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  FOREIGN KEY (session_id) REFERENCES sessions(id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id);";

    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        oc_error("SQLite schema error: %s", err);
        sqlite3_free(err);
        return OC_ERR;
    }
    return OC_OK;
}

session_store_t *session_store_open(const char *db_path) {
    session_store_t *store = calloc(1, sizeof(session_store_t));
    if (!store) return NULL;

    int rc = sqlite3_open(db_path, &store->db);
    if (rc != SQLITE_OK) {
        oc_error("Failed to open session DB: %s", sqlite3_errmsg(store->db));
        free(store);
        return NULL;
    }

    /* Enable WAL mode for better concurrency */
    sqlite3_exec(store->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(store->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    if (ensure_tables(store->db) != OC_OK) {
        sqlite3_close(store->db);
        free(store);
        return NULL;
    }

    return store;
}

void session_store_close(session_store_t *store) {
    if (!store) return;
    sqlite3_close(store->db);
    free(store);
}

/* Generate a random hex session ID */
static char *generate_session_id(void) {
    unsigned char buf[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < sizeof(buf)) return NULL;

    char *id = malloc(33);
    if (!id) return NULL;
    for (int i = 0; i < 16; i++) {
        sprintf(id + i * 2, "%02x", buf[i]);
    }
    return id;
}

char *session_store_create(session_store_t *store, const char *channel_id) {
    char *id = generate_session_id();
    if (!id) return NULL;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO sessions (id, channel_id, created_at, updated_at) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        free(id);
        return NULL;
    }

    time_t now = time(NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channel_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        oc_error("Failed to create session: %s", sqlite3_errmsg(store->db));
        sqlite3_finalize(stmt);
        free(id);
        return NULL;
    }

    sqlite3_finalize(stmt);
    return id;
}

int session_store_add_message(session_store_t *store, const char *session_id,
                               const char *role, const char *content) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO messages (session_id, role, content, created_at) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return OC_ERR;
    }

    time_t now = time(NULL);
    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, role, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* Update session timestamp */
    sqlite3_exec(store->db,
        "UPDATE sessions SET updated_at = strftime('%s','now') WHERE id = ?",
        NULL, NULL, NULL);

    return (rc == SQLITE_DONE) ? OC_OK : OC_ERR;
}

char *session_store_get_history(session_store_t *store, const char *session_id,
                                 int limit) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT role, content FROM messages WHERE session_id = ? "
                      "ORDER BY created_at DESC LIMIT ?";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 50);

    /* Build JSON array */
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { sqlite3_finalize(stmt); return NULL; }
    len += (size_t)snprintf(buf, cap, "[");

    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *role = (const char *)sqlite3_column_text(stmt, 0);
        const char *content = (const char *)sqlite3_column_text(stmt, 1);
        if (!role || !content) continue;

        size_t needed = strlen(role) + strlen(content) + 64;
        while (len + needed >= cap) {
            cap *= 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) { free(buf); sqlite3_finalize(stmt); return NULL; }
            buf = new_buf;
        }

        len += (size_t)snprintf(buf + len, cap - len, "%s{\"role\":\"%s\",\"content\":\"%s\"}",
                                 first ? "" : ",", role, content);
        first = false;
    }

    snprintf(buf + len, cap - len, "]");
    sqlite3_finalize(stmt);
    return buf;
}

int session_store_get_message_count(session_store_t *store, const char *session_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM messages WHERE session_id = ?";
    if (sqlite3_prepare_v2(store->db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int session_store_count_sessions(session_store_t *store) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "SELECT COUNT(*) FROM sessions", -1, &stmt, NULL) != SQLITE_OK) return 0;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int session_store_count_messages(session_store_t *store) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "SELECT COUNT(*) FROM messages", -1, &stmt, NULL) != SQLITE_OK) return 0;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}
