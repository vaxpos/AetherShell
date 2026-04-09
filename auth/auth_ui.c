#include "auth_ui.h"

#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <gdk/gdkkeysyms.h>
#include <stdlib.h>
#include <string.h>

#define DIALOG_WIDTH 420
#define DIALOG_HEIGHT 240

typedef struct {
    GMainLoop *loop;
    GtkWidget *window;
    GtkWidget *entry;
    char *password_out;
    int max_length;
    int result;
} AuthDialogState;

static gboolean ui_ready = FALSE;
static gboolean css_loaded = FALSE;

static void secure_clear(void *ptr, size_t len) {
    volatile unsigned char *p = ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static void clear_entry_text(GtkWidget *entry) {
    if (!GTK_IS_ENTRY(entry)) {
        return;
    }

    GtkEntryBuffer *buffer = gtk_entry_get_buffer(GTK_ENTRY(entry));
    if (!buffer) {
        return;
    }

    guint len = gtk_entry_buffer_get_length(buffer);
    if (len == 0) {
        return;
    }

    char *overwrite = g_malloc0((gsize)len + 1);
    memset(overwrite, ' ', len);
    gtk_entry_buffer_set_text(buffer, overwrite, (gint)len);
    gtk_entry_buffer_set_text(buffer, "", 0);
    secure_clear(overwrite, len);
    g_free(overwrite);
}

static gboolean is_wayland_session(void) {
    const char *session_type = g_getenv("XDG_SESSION_TYPE");
    const char *wayland_display = g_getenv("WAYLAND_DISPLAY");

    if (wayland_display && *wayland_display) {
        return TRUE;
    }

    return session_type && g_strcmp0(session_type, "wayland") == 0;
}

static void load_css(void) {
    if (css_loaded) {
        return;
    }

    const char *css =
        ".venom-auth-window {"
        "  background: transparent;"
        "}"
        ".venom-auth-card {"
        "  background: rgba(0, 0, 0, 0.26);"
        "  border: 2px solid rgba(0, 0, 0, 0.36);"
        "  border-radius: 18px;"
        "  padding: 24px;"
        "}"
        ".venom-auth-title {"
        "  color: #cdd6f4;"
        "  font-weight: 700;"
        "  font-size: 18px;"
        "}"
        ".venom-auth-message {"
        "  color: #a6adc8;"
        "  font-size: 12px;"
        "}"
        ".venom-auth-entry {"
        "  background: rgba(0, 0, 0, 0.36);"
        "  color: #cdd6f4;"
        "  border: 1px solid rgba(74, 74, 74, 0.36);"
        "  border-radius: 10px;"
        "  padding: 10px 12px;"
        "}"
        ".venom-auth-entry:focus {"
        "  border-color: rgba(85, 85, 85, 0.36);"
        "}"
        ".venom-auth-button {"
        "  background: #89b4fa;"
        "  color: #11111b;"
        "  border-radius: 10px;"
        "  padding: 8px 18px;"
        "}"
        ".venom-auth-button:hover {"
        "  background: #a6c8ff;"
        "}"
        ".venom-auth-cancel {"
        "  background: #313244;"
        "  color: #cdd6f4;"
        "  border-radius: 10px;"
        "  padding: 8px 18px;"
        "}"
        ".venom-auth-hint {"
        "  color: #7f849c;"
        "  font-size: 11px;"
        "}";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);

    GdkScreen *screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(
            screen,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    g_object_unref(provider);
    css_loaded = TRUE;
}

static void center_layer_shell_window(GtkWindow *window) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (!monitor) {
        return;
    }

    GdkRectangle workarea = {0};
    gdk_monitor_get_workarea(monitor, &workarea);

    gtk_layer_set_monitor(window, monitor);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT,
                         workarea.x + (workarea.width - DIALOG_WIDTH) / 2);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP,
                         workarea.y + (workarea.height - DIALOG_HEIGHT) / 2);
}

static gboolean focus_entry_idle(gpointer user_data) {
    GtkWidget *entry = GTK_WIDGET(user_data);
    if (!GTK_IS_WIDGET(entry)) {
        return G_SOURCE_REMOVE;
    }

    gtk_widget_grab_focus(entry);
    return G_SOURCE_REMOVE;
}

static void finish_dialog(AuthDialogState *state, int result) {
    if (!state) {
        return;
    }

    state->result = result;
    if (state->loop && g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
}

static void accept_dialog(GtkButton *button, gpointer user_data) {
    (void)button;

    AuthDialogState *state = user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(state->entry));
    size_t len = strlen(text);

    if (len == 0 || len >= (size_t)state->max_length) {
        return;
    }

    g_strlcpy(state->password_out, text, (gsize)state->max_length);
    finish_dialog(state, UI_RESULT_SUCCESS);
}

static void cancel_dialog(GtkButton *button, gpointer user_data) {
    (void)button;
    finish_dialog((AuthDialogState *)user_data, UI_RESULT_CANCEL);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;

    if (event->keyval == GDK_KEY_Escape) {
        finish_dialog((AuthDialogState *)user_data, UI_RESULT_CANCEL);
        return TRUE;
    }

    return FALSE;
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;
    finish_dialog((AuthDialogState *)user_data, UI_RESULT_CANCEL);
    return TRUE;
}

int auth_ui_init(void) {
    if (ui_ready) {
        return 0;
    }

    if (!gtk_init_check(NULL, NULL)) {
        return -1;
    }

    gtk_window_set_default_icon_name("dialog-password");
    load_css();
    ui_ready = TRUE;
    return 0;
}

void auth_ui_cleanup(void) {
}

int auth_ui_get_password(const char *message,
                         const char *icon_name,
                         char *password_out,
                         int max_length) {
    (void)icon_name;

    if (!ui_ready || !password_out || max_length <= 1) {
        return UI_RESULT_ERROR;
    }

    password_out[0] = '\0';

    AuthDialogState state = {
        .loop = g_main_loop_new(NULL, FALSE),
        .window = gtk_window_new(GTK_WINDOW_TOPLEVEL),
        .entry = NULL,
        .password_out = password_out,
        .max_length = max_length,
        .result = UI_RESULT_CANCEL,
    };

    if (!state.loop || !state.window) {
        if (state.loop) {
            g_main_loop_unref(state.loop);
        }
        if (state.window) {
            gtk_widget_destroy(state.window);
        }
        return UI_RESULT_ERROR;
    }

    gtk_widget_set_name(state.window, "venom-auth-window");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.window), "venom-auth-window");
    gtk_window_set_title(GTK_WINDOW(state.window), "Authentication Required");
    gtk_window_set_default_size(GTK_WINDOW(state.window), DIALOG_WIDTH, DIALOG_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(state.window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(state.window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_modal(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(state.window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_widget_set_size_request(state.window, DIALOG_WIDTH, DIALOG_HEIGHT);

    if (is_wayland_session() && gtk_layer_is_supported()) {
        gtk_layer_init_for_window(GTK_WINDOW(state.window));
        gtk_layer_set_namespace(GTK_WINDOW(state.window), "venom-auth");
        gtk_layer_set_layer(GTK_WINDOW(state.window), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(state.window), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(state.window), 0);
        center_layer_shell_window(GTK_WINDOW(state.window));
    } else {
        gtk_window_set_position(GTK_WINDOW(state.window), GTK_WIN_POS_CENTER_ALWAYS);
    }

    GtkWidget *outer = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(state.window), outer);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_name(card, "venom-auth-card");
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "venom-auth-card");
    gtk_container_add(GTK_CONTAINER(outer), card);

    GtkWidget *title = gtk_label_new("Authentication Required");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "venom-auth-title");
    gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 0);

    GtkWidget *body = gtk_label_new(message ? message : "Enter your password to continue.");
    gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_justify(GTK_LABEL(body), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(body, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(body), "venom-auth-message");
    gtk_box_pack_start(GTK_BOX(card), body, FALSE, FALSE, 0);

    state.entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(state.entry), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(state.entry), 0x2022);
    gtk_entry_set_activates_default(GTK_ENTRY(state.entry), TRUE);
    gtk_entry_set_input_purpose(GTK_ENTRY(state.entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_max_length(GTK_ENTRY(state.entry), max_length - 1);
    gtk_style_context_add_class(gtk_widget_get_style_context(state.entry), "venom-auth-entry");
    gtk_box_pack_start(GTK_BOX(card), state.entry, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new("Enter password and press Enter. ESC cancels.");
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "venom-auth-hint");
    gtk_box_pack_start(GTK_BOX(card), hint, FALSE, FALSE, 0);

    GtkWidget *actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(actions), 10);
    gtk_box_pack_end(GTK_BOX(card), actions, FALSE, FALSE, 0);

    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel), "venom-auth-cancel");
    gtk_container_add(GTK_CONTAINER(actions), cancel);

    GtkWidget *unlock = gtk_button_new_with_label("Authenticate");
    gtk_style_context_add_class(gtk_widget_get_style_context(unlock), "venom-auth-button");
    gtk_widget_set_can_default(unlock, TRUE);
    gtk_container_add(GTK_CONTAINER(actions), unlock);

    g_signal_connect(state.window, "delete-event", G_CALLBACK(on_window_delete), &state);
    g_signal_connect(state.window, "key-press-event", G_CALLBACK(on_key_press), &state);
    g_signal_connect(state.entry, "activate", G_CALLBACK(accept_dialog), &state);
    g_signal_connect(cancel, "clicked", G_CALLBACK(cancel_dialog), &state);
    g_signal_connect(unlock, "clicked", G_CALLBACK(accept_dialog), &state);

    gtk_widget_show_all(state.window);
    gtk_window_present(GTK_WINDOW(state.window));
    gtk_widget_grab_default(unlock);
    g_idle_add(focus_entry_idle, state.entry);

    g_main_loop_run(state.loop);

    clear_entry_text(state.entry);

    if (GTK_IS_WIDGET(state.window)) {
        gtk_widget_destroy(state.window);
    }

    g_main_loop_unref(state.loop);

    if (state.result != UI_RESULT_SUCCESS) {
        secure_clear(password_out, (size_t)max_length);
    }

    return state.result;
}
