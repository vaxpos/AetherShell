/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🖼️ Venom Basilisk - Window Module (Neon Grid Launcher)
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "window.h"
#include "search.h"
#include "commands.h"

extern BasiliskState *state;

// ═══════════════════════════════════════════════════════════════════════════
// CSS - Clean design, no shadows (compositor handles effects)
// ═══════════════════════════════════════════════════════════════════════════

static const gchar *css_data = 
    "* { background-color: rgba(0, 0, 0, 0.0); padding: 0; margin: 0; }"
    "window { background-color: transparent; }"
    "box { background-color: transparent; }"
    "grid { background-color: transparent; }"
    "viewport { background-color: transparent; }"
    "scrolledwindow { background-color: transparent; }"
    
    /* Main container - transparent with colored border */
    "#main-container {"
    "  background-color: rgba(0, 0, 0, 0.3);"
    "  border: 2px solid rgba(0, 255, 200, 0);"
    "  border-radius: 16px;"
    "  padding: 20px;"
    "}"
    
    /* Search entry - cyan border */
    "#search-entry {"
    "  background-color: transparent;"
    "  border: 1px solid #000000;"
    "  border-radius: 8px;"
    "  color: #ffffff;"
    "  font-size: 16px;"
    "  padding: 12px 16px;"
    "  min-height: 20px;"
    "}"
    "#search-entry:focus {"
    "  border-color: #000000;"
    "  outline: none;"
    "  box-shadow: none;"
    "}"
    "#search-entry, #search-entry text {"
    "  color: #ffffff;"
    "  caret-color: #ffffff;"
    "}"
    "#search-entry.search-hint, #search-entry.search-hint text {"
    "  color: #ffffff;"
    "}"
    
    /* App buttons */
    ".app-button {"
    "  background-color: transparent;"
    "  border: none;"
    "  border-radius: 8px;"
    "  padding: 2px;"
    "  min-width: 90px;"
    "  min-height: 90px;"
    "}"
    ".app-button:hover {"
    "  background-color: rgba(0, 255, 200, 0.06);"
    "}"
    
    /* Colored borders for variety */
    ".app-button.color-cyan { border-color: #00ffcc; }"
    ".app-button.color-pink { border-color: #ff00aa; }"
    ".app-button.color-purple { border-color: #aa00ff; }"
    ".app-button.color-green { border-color: #00ff66; }"
    ".app-button.color-blue { border-color: #0088ff; }"
    
    /* App icon and name */
    ".app-icon { margin-bottom: 4px; }"
    ".app-name {"
    "  color: #ffffff;"
    "  font-size: 11px;"
    "  font-weight: 500;"
    "}"
    
    /* Category tabs */
    ".category-button {"
    "  background-color: transparent;"
    "  border: none;"
    "  border-bottom: 3px solid transparent;"
    "  color: #888888;"
    "  font-size: 13px;"
    "  padding: 8px 16px;"
    "  min-width: 80px;"
    "}"
    ".category-button:hover {"
    "  color: #cccccc;"
    "}"
    ".category-button.active {"
    "  color: #ffffff;"
    "  border-bottom-color: #00ffcc;"
    "}"
    ".category-button.cat-dev.active { border-bottom-color: #00ff66; }"
    ".category-button.cat-sys.active { border-bottom-color: #aa00ff; }"
    ".category-button.cat-net.active { border-bottom-color: #ff00aa; }"
    ".category-button.cat-util.active { border-bottom-color: #0088ff; }"
    ".category-button.cat-other.active { border-bottom-color: #ffaa00; }";

// ═══════════════════════════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════════════════════════

static GtkWidget *category_buttons[CAT_COUNT];
static const gchar *color_classes[] = {"color-cyan", "color-pink", "color-purple", "color-green", "color-blue"};
static const gchar *search_hint_text = "BasiliskSearch ";

static void on_window_realize_disable_decorations(GtkWidget *widget, gpointer data) {
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
// Forward declarations
// ═══════════════════════════════════════════════════════════════════════════

void window_refresh_grid(void);

// ═══════════════════════════════════════════════════════════════════════════
// CSS Application
// ═══════════════════════════════════════════════════════════════════════════

void window_apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

// ═══════════════════════════════════════════════════════════════════════════
// App Button Creation
// ═══════════════════════════════════════════════════════════════════════════

static void on_app_clicked(GtkButton *button, gpointer data) {
    (void)button;
    const gchar *desktop_file = (const gchar *)data;
    
    GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(desktop_file);
    if (app_info) {
        g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, NULL);
        g_object_unref(app_info);
    }
    
    window_hide();
}

static GtkWidget* create_app_button(AppEntry *app, int index) {
    GtkWidget *button = gtk_button_new();
    gtk_widget_set_name(button, "app-button");
    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    gtk_style_context_add_class(ctx, "app-button");
    
    // Add color class based on index
    gtk_style_context_add_class(ctx, color_classes[index % 5]);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(button), vbox);
    
    // Icon
    GtkWidget *icon = NULL;
    if (app->icon) {
        GtkIconTheme *theme = gtk_icon_theme_get_default();
        if (g_path_is_absolute(app->icon)) {
            GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(app->icon, ICON_SIZE, ICON_SIZE, TRUE, NULL);
            if (pb) {
                icon = gtk_image_new_from_pixbuf(pb);
                g_object_unref(pb);
            }
        } else {
            GdkPixbuf *pb = gtk_icon_theme_load_icon(theme, app->icon, ICON_SIZE, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
            if (pb) {
                icon = gtk_image_new_from_pixbuf(pb);
                g_object_unref(pb);
            }
        }
    }
    if (!icon) {
        icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), ICON_SIZE);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "app-icon");
    gtk_box_pack_start(GTK_BOX(vbox), icon, FALSE, FALSE, 0);
    
    // Name (truncated)
    gchar *short_name = g_strndup(app->name, 12);
    if (strlen(app->name) > 12) {
        gchar *tmp = g_strdup_printf("%s..", short_name);
        g_free(short_name);
        short_name = tmp;
    }
    GtkWidget *label = gtk_label_new(short_name);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "app-name");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 10);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    g_free(short_name);
    
    g_signal_connect(button, "clicked", G_CALLBACK(on_app_clicked), app->desktop_file);
    
    return button;
}

// ═══════════════════════════════════════════════════════════════════════════
// Category Handling
// ═══════════════════════════════════════════════════════════════════════════

static void on_category_clicked(GtkButton *button, gpointer data) {
    AppCategory cat = GPOINTER_TO_INT(data);
    state->current_category = cat;
    
    // Update button styles
    for (int i = 0; i < CAT_COUNT; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(category_buttons[i]);
        if (i == (int)cat) {
            gtk_style_context_add_class(ctx, "active");
        } else {
            gtk_style_context_remove_class(ctx, "active");
        }
    }
    
    // Refresh grid
    window_refresh_grid();
    
    (void)button;
}

static GtkWidget* create_category_bar(void) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(hbox, 15);
    
    const gchar *names[] = {"All", "Development", "System", "Internet", "Utility", "Other"};
    const gchar *cat_classes[] = {"cat-all", "cat-dev", "cat-sys", "cat-net", "cat-util", "cat-other"};
    
    for (int i = 0; i < CAT_COUNT; i++) {
        GtkWidget *btn = gtk_button_new_with_label(names[i]);
        gtk_widget_set_name(btn, "category-button");
        GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(ctx, "category-button");
        gtk_style_context_add_class(ctx, cat_classes[i]);
        
        if (i == 0) {
            gtk_style_context_add_class(ctx, "active");
        }
        
        g_signal_connect(btn, "clicked", G_CALLBACK(on_category_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
        
        category_buttons[i] = btn;
    }
    
    return hbox;
}

// ═══════════════════════════════════════════════════════════════════════════
// Grid Refresh
// ═══════════════════════════════════════════════════════════════════════════

void window_refresh_grid(void) {
    // Clear existing children
    GList *children = gtk_container_get_children(GTK_CONTAINER(state->app_grid));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    
    // Get search text
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
    gchar *lower_search = (!state->search_hint_visible && search_text && strlen(search_text) > 0)
        ? g_utf8_strdown(search_text, -1) : NULL;
    
    int count = 0;
    int max_apps = GRID_COLS * GRID_ROWS;
    
    for (GList *l = state->app_cache; l != NULL && count < max_apps; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;
        
        // Category filter
        if (state->current_category != CAT_ALL && app->category != state->current_category) {
            continue;
        }
        
        // Search filter
        if (lower_search) {
            gchar *lower_name = g_utf8_strdown(app->name, -1);
            gboolean match = strstr(lower_name, lower_search) != NULL;
            g_free(lower_name);
            if (!match) continue;
        }
        
        GtkWidget *btn = create_app_button(app, count);
        gtk_grid_attach(GTK_GRID(state->app_grid), btn, count % GRID_COLS, count / GRID_COLS, 1, 1);
        count++;
    }
    
    g_free(lower_search);
    gtk_widget_show_all(state->app_grid);
}

// ═══════════════════════════════════════════════════════════════════════════
// Event Handlers
// ═══════════════════════════════════════════════════════════════════════════

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    
    if (event->keyval == GDK_KEY_Escape) {
        window_hide();
        return TRUE;
    }
    
    if (event->keyval == GDK_KEY_Return) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
        if (!state->search_hint_visible && commands_check_prefix(text)) {
            commands_execute(text);
            return TRUE;
        }
    }
    
    return FALSE;
}

static void on_search_changed(GtkEditable *editable, gpointer data) {
    (void)editable; (void)data;
    window_refresh_grid();
}

static void set_search_hint_visible(gboolean visible) {
    GtkStyleContext *ctx;

    if (!state || !state->search_entry) return;

    ctx = gtk_widget_get_style_context(state->search_entry);
    state->search_hint_visible = visible;

    if (visible) {
        gtk_style_context_add_class(ctx, "search-hint");
        gtk_entry_set_text(GTK_ENTRY(state->search_entry), search_hint_text);
        gtk_editable_set_position(GTK_EDITABLE(state->search_entry), 0);
    } else {
        gtk_style_context_remove_class(ctx, "search-hint");
        gtk_entry_set_text(GTK_ENTRY(state->search_entry), "");
    }
}

static gboolean on_search_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    const gchar *text;

    (void)widget;
    (void)event;
    (void)data;

    text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
    if (!text || text[0] == '\0') {
        set_search_hint_visible(TRUE);
    }

    return FALSE;
}

static gboolean on_search_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    gboolean printable;

    (void)widget;
    (void)data;

    if (!state->search_hint_visible) {
        return FALSE;
    }

    printable = g_unichar_isprint(gdk_keyval_to_unicode(event->keyval));
    if (printable || event->keyval == GDK_KEY_BackSpace || event->keyval == GDK_KEY_Delete) {
        set_search_hint_visible(FALSE);
    }

    return FALSE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Window Creation
// ═══════════════════════════════════════════════════════════════════════════

void window_init(void) {
    window_apply_css();
    
    // Main Window
    state->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state->window), "Basilisk");
    gtk_window_set_decorated(GTK_WINDOW(state->window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(state->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(state->window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(state->window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_keep_above(GTK_WINDOW(state->window), TRUE);
    gtk_widget_set_app_paintable(state->window, TRUE);
    gtk_window_set_default_size(GTK_WINDOW(state->window), WINDOW_WIDTH, WINDOW_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);
    g_signal_connect(state->window, "realize",
                     G_CALLBACK(on_window_realize_disable_decorations), NULL);
    
    // Transparency
    GdkScreen *screen = gtk_widget_get_screen(state->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(state->window, visual);
    
    // Main container
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_name(main_box, "main-container");
    gtk_container_add(GTK_CONTAINER(state->window), main_box);
    
    // Search entry
    state->search_entry = gtk_entry_new();
    gtk_widget_set_name(state->search_entry, "search-entry");
    gtk_box_pack_start(GTK_BOX(main_box), state->search_entry, FALSE, FALSE, 0);
    
    // Scrolled window for app grid
    state->scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->scroll_window), 
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(state->scroll_window, TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), state->scroll_window, TRUE, TRUE, 0);
    
    // App grid
    state->app_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(state->app_grid), 15);
    gtk_grid_set_column_spacing(GTK_GRID(state->app_grid), 15);
    gtk_widget_set_halign(state->app_grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state->app_grid, GTK_ALIGN_START);
    gtk_widget_set_margin_top(state->app_grid, 10);
    gtk_container_add(GTK_CONTAINER(state->scroll_window), state->app_grid);
    
    // Category bar
    state->category_bar = create_category_bar();
    gtk_box_pack_end(GTK_BOX(main_box), state->category_bar, FALSE, FALSE, 0);
    
    // Signals
    g_signal_connect(state->window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(state->search_entry, "changed", G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(state->search_entry, "focus-out-event", G_CALLBACK(on_search_focus_out), NULL);
    g_signal_connect(state->search_entry, "key-press-event", G_CALLBACK(on_search_entry_key_press), NULL);
    g_signal_connect(state->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    
    state->visible = FALSE;
    state->search_hint_visible = FALSE;
    state->current_category = CAT_ALL;
    set_search_hint_visible(TRUE);
}

// ═══════════════════════════════════════════════════════════════════════════
// Show / Hide
// ═══════════════════════════════════════════════════════════════════════════

void window_show(void) {
    if (!state->visible) {
        set_search_hint_visible(TRUE);
        state->current_category = CAT_ALL;
        
        // Reset category button styles
        for (int i = 0; i < CAT_COUNT; i++) {
            GtkStyleContext *ctx = gtk_widget_get_style_context(category_buttons[i]);
            if (i == 0) {
                gtk_style_context_add_class(ctx, "active");
            } else {
                gtk_style_context_remove_class(ctx, "active");
            }
        }
        
        window_refresh_grid();
        
        // Center on screen
        GdkDisplay *display = gdk_display_get_default();
        GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        
        gint x = geometry.x + (geometry.width - WINDOW_WIDTH) / 2;
        gint y = geometry.y + (geometry.height - WINDOW_HEIGHT) / 2;
        
        gtk_window_move(GTK_WINDOW(state->window), x, y);
        gtk_widget_show_all(state->window);
        gtk_window_present(GTK_WINDOW(state->window));
        
        // Focus search entry
        g_timeout_add(50, (GSourceFunc)gtk_widget_grab_focus, state->search_entry);
        
        state->visible = TRUE;
    }
}

void window_hide(void) {
    if (state->visible) {
        gtk_widget_hide(state->window);
        state->visible = FALSE;
    }
}

void window_toggle(void) {
    if (state->visible) {
        window_hide();
    } else {
        window_show();
    }
}

// Not needed anymore but keep for compatibility
void dropdown_show(void) {}
void dropdown_hide(void) {}
