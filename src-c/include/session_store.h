#ifndef SESSION_STORE_H
#define SESSION_STORE_H

#include <stdint.h>

/* Session store backed by SQLite */
typedef struct session_store session_store_t;

/* Open/create the session database */
session_store_t *session_store_open(const char *db_path);

/* Close the session store */
void session_store_close(session_store_t *store);

/* Create a new session, returns session_id (caller frees) */
char *session_store_create(session_store_t *store, const char *channel_id);

/* Add a message to a session */
int session_store_add_message(session_store_t *store, const char *session_id,
                               const char *role, const char *content);

/* Get conversation history as JSON array string (caller frees) */
char *session_store_get_history(session_store_t *store, const char *session_id,
                                 int limit);

/* Get session metadata */
int session_store_get_message_count(session_store_t *store, const char *session_id);

/* Count total active sessions */
int session_store_count_sessions(session_store_t *store);

/* Count total messages across all sessions */
int session_store_count_messages(session_store_t *store);

#endif /* SESSION_STORE_H */
