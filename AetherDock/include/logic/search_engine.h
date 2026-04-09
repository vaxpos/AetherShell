#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

#include <glib.h>

/* Execute math expression using bc */
/* Returns: Allocated string result (freed by caller) */
char *search_exec_math(const char *expr);

/* Search files using find */
/* Returns: GList of strings (paths) */
GList *search_exec_file(const char *term);

/* Format Web URL */
/* Returns: Allocated string URL */
char *search_get_web_url(const char *term, const char *engine);

/* AI Chat Logic */
/* Callback for streamed response */
typedef void (*AiResponseCallback)(const char *chunk, void *user_data);

/* Run AI query (async-ish if pushed to thread, but here we might just wrap the curl spawning) */
/* Actually, the UI uses g_spawn_async_with_pipes for streaming. 
   We can expose a function to start that process and return the PID/FDs */
gboolean search_start_ai_process(const char *query, GPid *pid, int *stdout_fd, GError **error);

#endif
