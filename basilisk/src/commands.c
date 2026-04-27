/*
 * ═══════════════════════════════════════════════════════════════════════════
 * ⚡ Venom Basilisk - Commands Module
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "commands.h"
#include "window.h"
#include "ai_chat.h"
#include <unistd.h>

extern BasiliskState *state;

static void on_dialog_realize_disable_decorations(GtkWidget *widget, gpointer data) {
    GdkWindow *gdk_window;

    (void)data;

    gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) return;

    if (GDK_IS_WAYLAND_WINDOW(gdk_window)) {
        gdk_wayland_window_announce_csd(gdk_window);
    } else {
        gdk_window_set_decorations(gdk_window, 0);
        gdk_window_set_functions(gdk_window, 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

static gchar* execute_sync(const gchar *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return g_strdup("Error");
    
    GString *output = g_string_new("");
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        g_string_append(output, buf);
    }
    pclose(fp);
    return g_string_free(output, FALSE);
}

// ═══════════════════════════════════════════════════════════════════════════
// Command Detection
// ═══════════════════════════════════════════════════════════════════════════

gboolean commands_check_prefix(const gchar *query) {
    if (!query) return FALSE;
    return g_str_has_prefix(query, CMD_VATER) ||
           g_str_has_prefix(query, CMD_MATH) ||
           g_str_has_prefix(query, CMD_FILE) ||
           g_str_has_prefix(query, CMD_GITHUB) ||
           g_str_has_prefix(query, CMD_GOOGLE) ||
           g_str_has_prefix(query, CMD_AI);
}

// ═══════════════════════════════════════════════════════════════════════════
// Terminal Command (vater:)
// ═══════════════════════════════════════════════════════════════════════════

void commands_execute_vater(const gchar *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    
    // Launch in terminal
    gchar *term_cmd = g_strdup_printf("x-terminal-emulator -e bash -c '%s; echo; echo Press Enter to close...; read'", cmd);
    g_spawn_command_line_async(term_cmd, NULL);
    g_free(term_cmd);
    
    window_hide();
}

// ═══════════════════════════════════════════════════════════════════════════
// Math (!=)
// ═══════════════════════════════════════════════════════════════════════════

void commands_execute_math(const gchar *expr) {
    if (!expr || strlen(expr) == 0) return;
    
    gchar *cmd = g_strdup_printf("echo \"%s\" | bc -l 2>&1", expr);
    gchar *result = execute_sync(cmd);
    g_strstrip(result);
    g_free(cmd);
    
    // Show result dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE,
        "%s\n= %s", expr, result);
    
    gtk_window_set_title(GTK_WINDOW(dialog), "Calculator");
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);
    g_signal_connect(dialog, "realize",
                     G_CALLBACK(on_dialog_realize_disable_decorations), NULL);
    
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "dialog { background: rgba(30, 30, 35, 0.95); }"
        "dialog label { color: #ffffff; font-size: 18px; }"
        "dialog button { color: #ffffff; }", -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(dialog),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(result);
    
    window_hide();
}

// ═══════════════════════════════════════════════════════════════════════════
// File Search (vafile:)
// ═══════════════════════════════════════════════════════════════════════════

static void on_file_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer dialog) {
    (void)box;
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
    const gchar *path = g_object_get_data(G_OBJECT(child), "path");
    
    if (path) {
        gchar *dir = g_path_get_dirname(path);
        gchar *uri = g_filename_to_uri(dir, NULL, NULL);
        if (uri) {
            g_app_info_launch_default_for_uri(uri, NULL, NULL);
            g_free(uri);
        }
        g_free(dir);
    }
    
    gtk_widget_destroy(GTK_WIDGET(dialog));
    window_hide();
}

void commands_execute_file_search(const gchar *term) {
    if (!term || strlen(term) == 0) return;
    
    gchar *cmd = g_strdup_printf("find ~ -iname \"*%s*\" -type f 2>/dev/null | head -20", term);
    gchar *output = execute_sync(cmd);
    g_free(cmd);
    
    // Create dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "File Search", GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE, NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);
    g_signal_connect(dialog, "realize",
                     G_CALLBACK(on_dialog_realize_disable_decorations), NULL);
    
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "dialog { background: rgba(30, 30, 35, 0.95); }"
        "dialog label { color: #ffffff; }"
        "dialog button { color: #ffffff; }"
        "list { background: transparent; }"
        "row { padding: 8px; }"
        "row:hover { background: rgba(0, 212, 255, 0.2); }", -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(dialog),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(content), scroll);
    
    GtkWidget *listbox = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), listbox);
    g_signal_connect(listbox, "row-activated", G_CALLBACK(on_file_row_activated), dialog);
    
    gchar **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i] && strlen(lines[i]) > 0; i++) {
        GtkWidget *label = gtk_label_new(lines[i]);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        g_object_set_data_full(G_OBJECT(label), "path", g_strdup(lines[i]), g_free);
        gtk_container_add(GTK_CONTAINER(listbox), label);
    }
    g_strfreev(lines);
    g_free(output);
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    window_hide();
}

// ═══════════════════════════════════════════════════════════════════════════
// Web Search (g:, s:)
// ═══════════════════════════════════════════════════════════════════════════

void commands_execute_web_search(const gchar *term, const gchar *engine) {
    if (!term || strlen(term) == 0) return;
    
    gchar *url;
    if (g_strcmp0(engine, "github") == 0) {
        url = g_strdup_printf("https://github.com/search?q=%s", term);
    } else {
        url = g_strdup_printf("https://www.google.com/search?q=%s", term);
    }
    
    gtk_show_uri_on_window(GTK_WINDOW(state->window), url, GDK_CURRENT_TIME, NULL);
    g_free(url);
    
    window_hide();
}

// ═══════════════════════════════════════════════════════════════════════════
// Main Execute
// ═══════════════════════════════════════════════════════════════════════════

void commands_execute(const gchar *query) {
    if (g_str_has_prefix(query, CMD_VATER)) {
        commands_execute_vater(query + strlen(CMD_VATER));
    }
    else if (g_str_has_prefix(query, CMD_MATH)) {
        commands_execute_math(query + strlen(CMD_MATH));
    }
    else if (g_str_has_prefix(query, CMD_FILE)) {
        commands_execute_file_search(query + strlen(CMD_FILE));
    }
    else if (g_str_has_prefix(query, CMD_GITHUB)) {
        commands_execute_web_search(query + strlen(CMD_GITHUB), "github");
    }
    else if (g_str_has_prefix(query, CMD_GOOGLE)) {
        commands_execute_web_search(query + strlen(CMD_GOOGLE), "google");
    }
    else if (g_str_has_prefix(query, CMD_AI)) {
        ai_chat_show(query + strlen(CMD_AI));
        window_hide();
    }
}
