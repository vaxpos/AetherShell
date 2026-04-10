/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🤖 Venom Basilisk - AI Chat Module (Admiral AI)
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "ai_chat.h"
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════════════════
// Data Structure
// ═══════════════════════════════════════════════════════════════════════════

typedef struct {
    GtkWidget *window;
    GtkWidget *text_view;
    GtkWidget *entry;
    GtkWidget *spinner;
    GtkWidget *status_label;
    gchar *response;
    gint char_index;
    guint type_timer;
    GPid pid;
    guint stdout_watch;
} AiChatData;

static void on_ai_window_realize_disable_decorations(GtkWidget *widget, gpointer user_data) {
    GdkWindow *gdk_window;

    (void)user_data;

    gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) return;

    gdk_window_set_decorations(gdk_window, 0);
    gdk_window_set_functions(gdk_window, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Typewriter Effect
// ═══════════════════════════════════════════════════════════════════════════

static gboolean typewriter_tick(gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    
    if (!data->response || data->response[data->char_index] == '\0') {
        data->type_timer = 0;
        gtk_label_set_text(GTK_LABEL(data->status_label), "Done.");
        return FALSE;
    }
    
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    
    // UTF-8 safe
    gchar *p = data->response + data->char_index;
    gchar *next = g_utf8_next_char(p);
    gint len = next - p;
    
    gtk_text_buffer_insert(buf, &end, p, len);
    data->char_index += len;
    
    // Scroll
    GtkTextMark *mark = gtk_text_buffer_create_mark(buf, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(data->text_view), mark, 0, TRUE, 0, 1);
    gtk_text_buffer_delete_mark(buf, mark);
    
    return TRUE;
}

static void start_typewriter(AiChatData *data) {
    if (data->type_timer > 0) g_source_remove(data->type_timer);
    data->char_index = 0;
    gtk_label_set_text(GTK_LABEL(data->status_label), "Typing...");
    data->type_timer = g_timeout_add(10, typewriter_tick, data);
}

// ═══════════════════════════════════════════════════════════════════════════
// API Response Handler
// ═══════════════════════════════════════════════════════════════════════════

static gboolean on_stdout_data(GIOChannel *src, GIOCondition cond, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    GIOStatus status;
    gchar *str;
    gsize len;
    
    if (cond & G_IO_IN) {
        status = g_io_channel_read_to_end(src, &str, &len, NULL);
        if (status == G_IO_STATUS_NORMAL) {
            if (data->response) {
                gchar *tmp = g_strconcat(data->response, str, NULL);
                g_free(data->response);
                data->response = tmp;
            } else {
                data->response = g_strdup(str);
            }
            g_free(str);
        }
    }
    
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        gtk_spinner_stop(GTK_SPINNER(data->spinner));
        if (data->response) {
            start_typewriter(data);
        } else {
            gtk_label_set_text(GTK_LABEL(data->status_label), "No response.");
        }
        data->stdout_watch = 0;
        return FALSE;
    }
    
    return TRUE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Fetch Response
// ═══════════════════════════════════════════════════════════════════════════

static void fetch_response(AiChatData *data, const gchar *query) {
    if (!query || strlen(query) == 0) return;
    
    // Reset
    if (data->response) { g_free(data->response); data->response = NULL; }
    if (data->type_timer > 0) { g_source_remove(data->type_timer); data->type_timer = 0; }
    
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view)), "", -1);
    gtk_label_set_text(GTK_LABEL(data->status_label), "Admiral is thinking...");
    gtk_spinner_start(GTK_SPINNER(data->spinner));
    
    // Build curl command
    gchar *encoded = g_uri_escape_string(query, NULL, TRUE);
    gchar *url = g_strdup_printf("https://text.pollinations.ai/%s", encoded);
    g_free(encoded);
    
    gchar *argv[] = {"curl", "-s", url, NULL};
    GError *error = NULL;
    gint stdout_fd;
    
    if (g_spawn_async_with_pipes(NULL, argv, NULL,
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
        NULL, NULL, &data->pid, NULL, &stdout_fd, NULL, &error)) {
        
        GIOChannel *ch = g_io_channel_unix_new(stdout_fd);
        data->stdout_watch = g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_stdout_data, data);
        g_io_channel_unref(ch);
    } else {
        gtk_label_set_text(GTK_LABEL(data->status_label), "Connection failed.");
        gtk_spinner_stop(GTK_SPINNER(data->spinner));
        g_error_free(error);
    }
    
    g_free(url);
}

// ═══════════════════════════════════════════════════════════════════════════
// Entry Handler
// ═══════════════════════════════════════════════════════════════════════════

static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    const gchar *text = gtk_entry_get_text(entry);
    
    if (!text || strlen(text) == 0) return;
    
    if (g_str_has_prefix(text, "ai:")) {
        fetch_response(data, text + 3);
    } else {
        fetch_response(data, text);
    }
    
    // Clear entry after sending
    gtk_entry_set_text(entry, "");
}

// ═══════════════════════════════════════════════════════════════════════════
// Key Handler (Escape to close)
// ═══════════════════════════════════════════════════════════════════════════

static gboolean on_ai_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)user_data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(widget);
        return TRUE;
    }
    return FALSE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Cleanup
// ═══════════════════════════════════════════════════════════════════════════

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AiChatData *data = (AiChatData *)user_data;
    if (data->type_timer > 0) g_source_remove(data->type_timer);
    if (data->stdout_watch > 0) g_source_remove(data->stdout_watch);
    if (data->response) g_free(data->response);
    g_free(data);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main Show Function
// ═══════════════════════════════════════════════════════════════════════════

void ai_chat_show(const gchar *initial_query) {
    AiChatData *data = g_new0(AiChatData, 1);
    
    // Apply CSS GLOBALLY for ai-window (only once)
    static gboolean css_applied = FALSE;
    if (!css_applied) {
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css,
            /* Main window */
            "#ai-window {"
            "  background: rgba(0, 0, 0, 0.53);"
            "  border-radius: 16px;"
            "  border: 2px solid #00d4ff;"
            "  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5);"
            "}"
            
            /* Main container */
            "#ai-main-box {"
            "  background: transparent;"
            "}"
            
            /* Header */
            "#ai-header {"
            "  background: rgba(0, 212, 255, 0.1);"
            "  border-radius: 12px 12px 0 0;"
            "  padding: 12px 16px;"
            "}"
            ".ai-title {"
            "  font-size: 22px;"
            "  font-weight: bold;"
            "  color: #00d4ff;"
            "}"
            ".ai-beta {"
            "  background: #00d4ff;"
            "  color: #1a1a2e;"
            "  padding: 4px 12px;"
            "  border-radius: 12px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
            
            /* Entry */
            "#ai-entry {"
            "  background: rgba(255, 255, 255, 0.08);"
            "  border: 2px solid rgba(0, 212, 255, 0.4);"
            "  border-radius: 10px;"
            "  padding: 14px 16px;"
            "  color: #ffffff;"
            "  font-size: 15px;"
            "  caret-color: #00d4ff;"
            "}"
            "#ai-entry:focus {"
            "  border-color: #00d4ff;"
            "  background: rgba(255, 255, 255, 0.12);"
            "}"
            
            /* Response area */
            "#ai-response-scroll {"
            "  background: rgba(0, 0, 0, 0.3);"
            "  border-radius: 10px;"
            "  border: 1px solid rgba(255, 255, 255, 0.1);"
            "}"
            "#ai-textview {"
            "  background: transparent;"
            "  font-size: 15px;"
            "  color: #e0e0e0;"
            "}"
            "#ai-textview text {"
            "  background: transparent;"
            "}"
            
            /* Footer */
            "#ai-footer {"
            "  padding: 8px 0;"
            "}"
            "#ai-status {"
            "  color: #888888;"
            "  font-size: 12px;"
            "}",
            -1, NULL);
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_USER + 100
        );
        g_object_unref(css);
        css_applied = TRUE;
    }
    
    // Window
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(data->window), "Admiral AI");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 650, 480);
    gtk_window_set_position(GTK_WINDOW(data->window), GTK_WIN_POS_CENTER);
    gtk_window_set_decorated(GTK_WINDOW(data->window), FALSE);
    g_signal_connect(data->window, "realize",
                     G_CALLBACK(on_ai_window_realize_disable_decorations), NULL);
    gtk_widget_set_app_paintable(data->window, TRUE);
    gtk_widget_set_name(data->window, "ai-window");
    
    // RGBA
    GdkScreen *screen = gtk_widget_get_screen(data->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(data->window, visual);
    
    // Main Layout
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(main_box, "ai-main-box");
    gtk_container_add(GTK_CONTAINER(data->window), main_box);
    
    // Header
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_name(header, "ai-header");
    GtkWidget *icon = gtk_image_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    GtkWidget *title = gtk_label_new("Admiral AI");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "ai-title");
    GtkWidget *beta = gtk_label_new("BETA");
    gtk_style_context_add_class(gtk_widget_get_style_context(beta), "ai-beta");
    gtk_box_pack_start(GTK_BOX(header), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), beta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);
    
    // Content box with padding
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_pack_start(GTK_BOX(main_box), content, TRUE, TRUE, 0);
    
    // Entry
    data->entry = gtk_entry_new();
    gtk_widget_set_name(data->entry, "ai-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->entry), "Ask Admiral anything...");
    if (initial_query) {
        gtk_entry_set_text(GTK_ENTRY(data->entry), initial_query);
    }
    g_signal_connect(data->entry, "activate", G_CALLBACK(on_entry_activate), data);
    gtk_box_pack_start(GTK_BOX(content), data->entry, FALSE, FALSE, 0);
    
    // Response
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(scroll, "ai-response-scroll");
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    data->text_view = gtk_text_view_new();
    gtk_widget_set_name(data->text_view, "ai-textview");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(data->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(data->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(data->text_view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(data->text_view), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(data->text_view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(data->text_view), 12);
    gtk_container_add(GTK_CONTAINER(scroll), data->text_view);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    
    // Footer
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(footer, "ai-footer");
    data->spinner = gtk_spinner_new();
    data->status_label = gtk_label_new("Press Enter to ask Admiral");
    gtk_widget_set_name(data->status_label, "ai-status");
    gtk_box_pack_start(GTK_BOX(footer), data->spinner, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer), data->status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), footer, FALSE, FALSE, 0);
    
    // Signals
    g_signal_connect(data->window, "key-press-event", G_CALLBACK(on_ai_key_press), data);
    g_signal_connect(data->window, "destroy", G_CALLBACK(on_window_destroy), data);
    
    gtk_widget_show_all(data->window);
    gtk_widget_grab_focus(data->entry);
    
    if (initial_query && strlen(initial_query) > 0) {
        fetch_response(data, initial_query);
    }
}
