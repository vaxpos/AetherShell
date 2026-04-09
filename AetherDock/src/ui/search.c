#include "search.h"
#include "launcher.h"
#include "logic/search_engine.h"
#include "logic/app_manager.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

/* Helper to close dialogs */
static void on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    (void)response_id; (void)user_data;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

typedef struct {
     GtkWidget *window;
     GtkWidget *text_view;
     GPid child_pid;
     int stdout_fd;
} AiChatContext;

/* Math */
void show_math_result_dialog(const char *result, GtkWidget *parent) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               "Math Result: %s", result);
    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL);
    gtk_widget_show_all(dialog);
}

void execute_math(const char *expr, GtkWidget *parent) {
    char *result = search_exec_math(expr);
    if (result) {
        show_math_result_dialog(result, parent);
        g_free(result);
    } else {
        show_math_result_dialog("Error or empty", parent);
    }
}

/* Files */
void show_file_results_dialog(GList *files, GtkWidget *parent) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("File Search Results",
                                                    GTK_WINDOW(parent),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close", GTK_RESPONSE_CLOSE,
                                                    NULL);
    gtk_widget_set_size_request(dialog, 600, 400);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(content_area), scrolled, TRUE, TRUE, 0);
    
    GtkWidget *list_box = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scrolled), list_box);
    
    for (GList *l = files; l != NULL; l = l->next) {
        GtkWidget *row_label = gtk_label_new((char *)l->data);
        gtk_label_set_xalign(GTK_LABEL(row_label), 0);
        gtk_list_box_insert(GTK_LIST_BOX(list_box), row_label, -1);
    }
    
    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL);
}

void execute_file_search(const char *term, GtkWidget *parent) {
    GList *files = search_exec_file(term);
    if (files) {
        show_file_results_dialog(files, parent);
        g_list_free_full(files, g_free);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(parent),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "No files found for '%s'", term);
        g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL);
        gtk_widget_show_all(dialog);
    }
}

/* Web */
void execute_web_search(const char *term, const char *engine, GtkWidget *parent, GtkWidget **stack_ptr, GtkWidget **entry_ptr, GtkWidget **results_ptr) {
    (void)stack_ptr; (void)entry_ptr; (void)results_ptr;
    
    char *url = search_get_web_url(term, engine);
    if (url) {
        gtk_show_uri_on_window(GTK_WINDOW(parent), url, GDK_CURRENT_TIME, NULL);
        g_free(url);
    }
}

/* AI */
static gboolean ai_read_output(GIOChannel *source, GIOCondition condition, gpointer data) {
    AiChatContext *ctx = (AiChatContext *)data;
    if (condition & G_IO_IN) {
        gchar *line = NULL;
        gsize length = 0;
        GError *error = NULL;
        
        while (g_io_channel_read_line(source, &line, &length, NULL, &error) == G_IO_STATUS_NORMAL) {
            if (line) {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->text_view));
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(buffer, &end);
                gtk_text_buffer_insert(buffer, &end, line, length);
                g_free(line);
            }
        }
        if (error) g_error_free(error);
    }
    
    if (condition & (G_IO_HUP | G_IO_ERR)) {
        return FALSE; /* Stop watch */
    }
    return TRUE;
}

void execute_ai_chat(const char *query, GtkWidget *parent) {
    AiChatContext *ctx = g_new0(AiChatContext, 1);
    
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "AI Chat Response");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent));
    
    ctx->window = window;
    ctx->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ctx->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ctx->text_view), GTK_WRAP_WORD);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), ctx->text_view);
    gtk_container_add(GTK_CONTAINER(window), scrolled);
    
    g_object_set_data_full(G_OBJECT(window), "ctx", ctx, g_free);
    
    GError *error = NULL;
    if (search_start_ai_process(query, &ctx->child_pid, &ctx->stdout_fd, &error)) {
        GIOChannel *channel = g_io_channel_unix_new(ctx->stdout_fd);
        g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, ai_read_output, ctx);
        g_io_channel_unref(channel);
    } else {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->text_view));
        gtk_text_buffer_set_text(buffer, "Failed to start AI process.", -1);
        if (error) g_error_free(error);
    }
    
    gtk_widget_show_all(window);
}

void execute_vater(const char *cmd, GtkWidget *parent) {
    (void)parent;
    g_spawn_command_line_async(cmd, NULL);
}

/* Stub for password dialog if needed or removed */
void show_password_dialog(const char *cmd, const char *pass, GtkWidget *parent) {
    (void)cmd; (void)pass; (void)parent;
}

/* Main search orchestrator */
void perform_search(const char *text, GtkWidget *stack, GtkWidget *results_view, GtkWidget *window) {
    (void)window;
    if (!text || strlen(text) == 0) {
        GtkWidget *page0 = gtk_stack_get_child_by_name(GTK_STACK(stack), "page0");
        if (page0) gtk_stack_set_visible_child(GTK_STACK(stack), page0);
        return;
    }
    
    /* Clear previous results */
    GList *children = gtk_container_get_children(GTK_CONTAINER(results_view));
    g_list_free_full(children, (GDestroyNotify)gtk_widget_destroy);
    
    /* Show results page */
    GtkWidget *res_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "search_results");
    if (res_page) gtk_stack_set_visible_child(GTK_STACK(stack), res_page);
    
    /* Special prefixes handled in activate */
    /* If normal search, scan apps */
    if (strchr(text, ':') == NULL) {
        GList *apps = app_mgr_scan_apps();
        char *lower_text = g_utf8_strdown(text, -1);
        
        for (GList *l = apps; l != NULL; l = l->next) {
            AppInfo *info = (AppInfo *)l->data;
            char *lower_name = g_utf8_strdown(info->name, -1);
            
            if (strstr(lower_name, lower_text) != NULL) {
                 GtkWidget *btn = gtk_button_new();
                 GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                 
                 GtkWidget *icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_MENU);
                 GtkWidget *label = gtk_label_new(info->name);
                 
                 gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
                 gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
                 gtk_container_add(GTK_CONTAINER(btn), box);
                 
                 g_object_set_data_full(G_OBJECT(btn), "desktop-file", g_strdup(info->desktop_file_path), g_free);
                 g_signal_connect(btn, "clicked", G_CALLBACK(on_launcher_app_clicked), (gpointer)"app");
                 
                 gtk_box_pack_start(GTK_BOX(results_view), btn, FALSE, FALSE, 0);
            }
            g_free(lower_name);
        }
        
        g_free(lower_text);
        app_mgr_free_list(apps);
    }
    
    gtk_widget_show_all(results_view);
}
