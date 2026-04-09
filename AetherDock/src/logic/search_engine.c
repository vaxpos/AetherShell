#include "logic/search_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char *search_exec_math(const char *expr) {
    if (!expr) return NULL;
    gchar *cmd = g_strdup_printf("echo \"%s\" | bc -l", expr);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        g_free(cmd);
        return NULL;
    }
    
    char buffer[1024];
    GString *output = g_string_new("");
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        g_string_append(output, buffer);
    }
    pclose(fp);
    g_free(cmd);
    
    g_strstrip(output->str);
    return g_string_free(output, FALSE);
}

GList *search_exec_file(const char *term) {
    if (!term) return NULL;
    
    gchar *cmd = g_strdup_printf("find ~ -name \"*%s*\" 2>/dev/null | head -n 20", term);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        g_free(cmd);
        return NULL;
    }
    
    GList *files = NULL;
    char path[1035];
    while (fgets(path, sizeof(path), fp) != NULL) {
        path[strcspn(path, "\n")] = 0;
        if (strlen(path) > 0) {
            files = g_list_append(files, g_strdup(path));
        }
    }
    pclose(fp);
    g_free(cmd);
    return files;
}

char *search_get_web_url(const char *term, const char *engine) {
    if (!term) return NULL;
    
    if (g_strcmp0(engine, "github") == 0) {
        return g_strdup_printf("https://github.com/search?q=%s", term);
    } else {
        return g_strdup_printf("https://www.google.com/search?q=%s", term);
    }
}

gboolean search_start_ai_process(const char *query, GPid *pid, int *stdout_fd, GError **error) {
    gchar *encoded_query = g_uri_escape_string(query, NULL, TRUE);
    gchar *url = g_strdup_printf("https://text.pollinations.ai/%s", encoded_query);
    g_free(encoded_query);
    
    gchar *argv[] = {"curl", "-s", url, NULL};
    gboolean success = g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL, pid, NULL, stdout_fd, NULL, error);
    
    g_free(url);
    return success;
}

