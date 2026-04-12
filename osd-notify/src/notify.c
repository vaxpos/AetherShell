#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gtk-layer-shell.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#include <time.h>

// --- إعدادات التصميم الأساسية ---
#define NOTIFY_WIDTH 340
#define MARGIN_X 20
#define MARGIN_Y 50
#define SPACING 10
#define DEFAULT_TIMEOUT 5000
#define MAX_HISTORY 50

// --- هيكل بيانات السجل (History Item) ---
typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    char *summary;
    char *body;
    gint64 timestamp;
} NotificationItem;

// --- هيكل النافذة النشطة ---
typedef struct {
    guint32 id;
    char *app_name;
    char *icon_path;
    GtkWidget *icon_img;
    GtkWidget *title_lbl;
    GtkWidget *body_lbl;
    GtkWidget *win;
    guint timeout_source;
} VenomNotification;

// --- هيكل بيانات الأزرار ---
typedef struct {
    guint32 notification_id;
    char *action_key;
} ActionData;

GList *active_notifications = NULL;
GList *history_list = NULL;
guint32 id_counter = 1;
gboolean do_not_disturb = FALSE;
GDBusConnection *dbus_connection = NULL;
gboolean notify_use_layer_shell = FALSE;

// --- واجهات D-Bus ---
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.Notifications'>"
  "    <method name='Notify'>"
  "      <arg type='s' name='app_name' direction='in'/>"
  "      <arg type='u' name='replaces_id' direction='in'/>"
  "      <arg type='s' name='app_icon' direction='in'/>"
  "      <arg type='s' name='summary' direction='in'/>"
  "      <arg type='s' name='body' direction='in'/>"
  "      <arg type='as' name='actions' direction='in'/>"
  "      <arg type='a{sv}' name='hints' direction='in'/>"
  "      <arg type='i' name='expire_timeout' direction='in'/>"
  "      <arg type='u' name='id' direction='out'/>"
  "    </method>"
  "    <method name='GetCapabilities'><arg type='as' name='caps' direction='out'/></method>"
  "    <method name='GetServerInformation'><arg type='s' name='name' direction='out'/><arg type='s' name='vendor' direction='out'/><arg type='s' name='ver' direction='out'/><arg type='s' name='spec' direction='out'/></method>"
  "    <method name='CloseNotification'><arg type='u' name='id' direction='in'/></method>"
  "    <signal name='NotificationClosed'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='u' name='reason'/>"
  "    </signal>"
  "    <signal name='ActionInvoked'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='s' name='action_key'/>"
  "    </signal>"
  "  </interface>"
  "  <interface name='org.venom.NotificationHistory'>"
  "    <method name='GetHistory'><arg type='a(ussss)' name='notifications' direction='out'/></method>"
  "    <method name='ClearHistory'/>"
  "    <method name='SetDoNotDisturb'><arg type='b' name='enabled' direction='in'/></method>"
  "    <method name='GetDoNotDisturb'><arg type='b' name='enabled' direction='out'/></method>"
  "    <signal name='HistoryUpdated'/>"
  "    <signal name='DoNotDisturbChanged'><arg type='b' name='enabled'/></signal>"
  "  </interface>"
  "</node>";

// --- تعريف مسبق للدوال ---
void close_notification(guint32 id, guint reason);
void emit_history_updated_signal(GDBusConnection *connection);
void add_to_history(guint32 id, const char *app, const char *icon, const char *summary, const char *body);
void clear_history();
static NotificationItem *find_history_item_by_id(guint32 id);
static VenomNotification *find_active_notification_by_id(guint32 id);
static void update_notification_icon(GtkWidget *image, const char *icon);
static void update_notification_content(VenomNotification *notification, const char *icon, const char *summary, const char *body);
static guint32 create_new_notification(const char *app_name, guint32 replaces_id, const char *summary, const char *body, const char *icon, GVariant *actions, gint timeout);
static gboolean notify_is_wayland_session(void);
static gboolean draw_notification_background(GtkWidget *widget, cairo_t *cr, gpointer data);

// --- إدارة السجل (History Logic) ---

void add_to_history(guint32 id, const char *app, const char *icon, const char *summary, const char *body) {
    NotificationItem *item = g_new0(NotificationItem, 1);
    item->id = id;
    item->app_name = g_strdup(app);
    item->icon_path = g_strdup(icon);
    item->summary = g_strdup(summary);
    item->body = g_strdup(body);
    item->timestamp = time(NULL);

    // إضافة لأول القائمة (الأحدث أولاً)
    history_list = g_list_prepend(history_list, item);

    // تنظيف القديم إذا تجاوزنا الحد المسموح
    if (g_list_length(history_list) > MAX_HISTORY) {
        GList *last = g_list_last(history_list);
        NotificationItem *old_item = (NotificationItem *)last->data;
        g_free(old_item->app_name);
        g_free(old_item->icon_path);
        g_free(old_item->summary);
        g_free(old_item->body);
        g_free(old_item);
        history_list = g_list_delete_link(history_list, last);
    }
}

void clear_history() {
    GList *l;
    for (l = history_list; l != NULL; l = l->next) {
        NotificationItem *item = (NotificationItem *)l->data;
        g_free(item->app_name);
        g_free(item->icon_path);
        g_free(item->summary);
        g_free(item->body);
        g_free(item);
    }
    g_list_free(history_list);
    history_list = NULL;
}

static NotificationItem *find_history_item_by_id(guint32 id) {
    for (GList *l = history_list; l != NULL; l = l->next) {
        NotificationItem *item = (NotificationItem *)l->data;
        if (item->id == id) {
            return item;
        }
    }
    return NULL;
}

static VenomNotification *find_active_notification_by_id(guint32 id) {
    for (GList *l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *notification = (VenomNotification *)l->data;
        if (notification->id == id) {
            return notification;
        }
    }
    return NULL;
}

// دالة مساعدة لإرسال إشارة "تحديث السجل"
void emit_history_updated_signal(GDBusConnection *connection) {
    // استخدم المتغير العام إذا لم نحصل على connection
    if (!connection) connection = dbus_connection;
    if (!connection) return;
    
    g_dbus_connection_emit_signal(connection,
                                  NULL,
                                  "/org/freedesktop/Notifications",
                                  "org.venom.NotificationHistory",
                                  "HistoryUpdated",
                                  NULL, NULL);
}

// --- إدارة الـ CSS (التصميم) ---
void load_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css = 
        "window.venom-notify {"
        "   background-color: rgba(0, 0, 0, 0.392);"
        "   background: rgba(0, 0, 0, 0.392);;"
        "   border: 1.5px solid rgba(0, 255, 255, 0.8);"
        "   border-radius: 8px;"
        "}"
        "label.notify-title {"
        "   color: #00FFFF;"
        "   font-weight: bold;"
        "   font-size: 11pt;"
        "}"
        "label.notify-body {"
        "   color: #EEEEEE;"
        "   font-size: 10pt;"
        "}"
        "button {"
        "   background-color: #222222;"
        "   color: #00FFFF;"
        "   border: 1px solid #00FFFF;"
        "   border-radius: 4px;"
        "   padding: 4px;"
        "}"
        "button:hover {"
        "   background-color: #00FFFF;"
        "   color: #000000;"
        "}";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    GdkScreen *screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(screen,
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}

static gboolean notify_is_wayland_session(void) {
#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
    return FALSE;
#endif
}

static gboolean draw_notification_background(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    gint width = gtk_widget_get_allocated_width(widget);
    gint height = gtk_widget_get_allocated_height(widget);
    const double radius = 8.0;

    if (width <= 0 || height <= 0) {
        return FALSE;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_new_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI / 2.0, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI / 2.0);
    cairo_arc(cr, radius, height - radius, radius, G_PI / 2.0, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.392);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.392);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    return FALSE;
}

// --- معالجة أحداث الأزرار (Actions) ---
void on_action_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ActionData *data = (ActionData *)user_data;
    if (dbus_connection) {
        g_dbus_connection_emit_signal(dbus_connection, NULL, "/org/freedesktop/Notifications",
                                      "org.freedesktop.Notifications", "ActionInvoked",
                                      g_variant_new("(us)", data->notification_id, data->action_key), NULL);
    }
    close_notification(data->notification_id, 2); // 2 = Dismissed by user
}

void free_action_data(gpointer data, GClosure *closure) {
    (void)closure;
    ActionData *action_data = (ActionData *)data;
    g_free(action_data->action_key);
    g_free(action_data);
}

// --- إعادة تموضع النوافذ ---
void reposition_notifications() {
    int count = 0;
    GdkRectangle workarea = {0};
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (monitor) {
        gdk_monitor_get_workarea(monitor, &workarea);
    }

    for (GList *l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *n = (VenomNotification *)l->data;
        gint width = NOTIFY_WIDTH;
        gint height = 0;
        gtk_window_get_size(GTK_WINDOW(n->win), &width, &height);
        if (height <= 0) {
            GtkRequisition min_req;
            gtk_widget_get_preferred_size(n->win, &min_req, NULL);
            height = min_req.height;
        }

        int y = MARGIN_Y + (count * (height + SPACING));
        if (notify_use_layer_shell) {
            gtk_layer_set_margin(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_TOP, y);
            gtk_layer_set_margin(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_RIGHT, MARGIN_X);
        } else {
            int x = workarea.width - width - MARGIN_X;
            gtk_window_move(GTK_WINDOW(n->win), x, y);
        }
        count++;
    }
}

gboolean on_timeout(gpointer data) {
    close_notification(GPOINTER_TO_UINT(data), 1); // 1 = Expired
    return FALSE; 
}

void close_notification(guint32 id, guint reason) {
    GList *l;
    for (l = active_notifications; l != NULL; l = l->next) {
        VenomNotification *n = (VenomNotification *)l->data;
        if (n->id == id) {
            if (n->timeout_source > 0) g_source_remove(n->timeout_source);
            
            gtk_widget_destroy(n->win);
            active_notifications = g_list_delete_link(active_notifications, l);
            g_free(n->app_name);
            g_free(n->icon_path);
            g_free(n);
            reposition_notifications();
            
            // إرسال إشارة الإغلاق
            if (dbus_connection) {
                g_dbus_connection_emit_signal(dbus_connection, NULL, "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "NotificationClosed",
                                              g_variant_new("(uu)", id, reason), NULL);
            }
            break;
        }
    }
}

static void update_notification_icon(GtkWidget *image, const char *icon) {
    if (!GTK_IS_IMAGE(image)) {
        return;
    }

    if (icon && strlen(icon) > 0) {
        if (g_path_is_absolute(icon) || g_file_test(icon, G_FILE_TEST_EXISTS)) {
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(icon, 48, 48, TRUE, NULL);
            if (pixbuf) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
                g_object_unref(pixbuf);
                return;
            }
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(image), icon, GTK_ICON_SIZE_DIALOG);
            gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
            return;
        }
    }

    gtk_image_set_from_icon_name(GTK_IMAGE(image), "dialog-information", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
}

static void update_notification_content(VenomNotification *notification, const char *icon, const char *summary, const char *body) {
    if (!notification) {
        return;
    }

    g_free(notification->icon_path);
    notification->icon_path = g_strdup(icon);
    update_notification_icon(notification->icon_img, icon);

    gtk_label_set_text(GTK_LABEL(notification->title_lbl), summary ? summary : "");

    if (notification->body_lbl) {
        if (body && strlen(body) > 0) {
            GError *error = NULL;
            if (pango_parse_markup(body, -1, 0, NULL, NULL, NULL, &error)) {
                gtk_label_set_markup(GTK_LABEL(notification->body_lbl), body);
            } else {
                gtk_label_set_text(GTK_LABEL(notification->body_lbl), body);
                g_clear_error(&error);
            }
            gtk_widget_show(notification->body_lbl);
        } else {
            gtk_label_set_text(GTK_LABEL(notification->body_lbl), "");
            gtk_widget_hide(notification->body_lbl);
        }
    }

    gtk_widget_show_all(notification->win);
    gtk_window_resize(GTK_WINDOW(notification->win), NOTIFY_WIDTH, 1);
    reposition_notifications();
}

// --- إنشاء الإشعار وعرضه ---
static guint32 create_new_notification(const char *app_name, guint32 replaces_id, const char *summary, const char *body, const char *icon, GVariant *actions, gint timeout) {
    // إذا كان وضع عدم الإزعاج مفعل، أضف للسجل بشكل صامت فقط
    if (do_not_disturb) {
        NotificationItem *existing_item = replaces_id ? find_history_item_by_id(replaces_id) : NULL;
        if (existing_item) {
            g_free(existing_item->app_name);
            g_free(existing_item->icon_path);
            g_free(existing_item->summary);
            g_free(existing_item->body);
            existing_item->app_name = g_strdup(app_name);
            existing_item->icon_path = g_strdup(icon);
            existing_item->summary = g_strdup(summary);
            existing_item->body = g_strdup(body);
            existing_item->timestamp = time(NULL);
            emit_history_updated_signal(NULL);
            return existing_item->id;
        }

        guint32 new_id = id_counter++;
        add_to_history(new_id, app_name, icon, summary, body);
        emit_history_updated_signal(NULL);
        return new_id;
    }

    VenomNotification *existing_notification = replaces_id ? find_active_notification_by_id(replaces_id) : NULL;
    if (existing_notification) {
        NotificationItem *existing_item = find_history_item_by_id(existing_notification->id);
        if (existing_item) {
            g_free(existing_item->app_name);
            g_free(existing_item->icon_path);
            g_free(existing_item->summary);
            g_free(existing_item->body);
            existing_item->app_name = g_strdup(app_name);
            existing_item->icon_path = g_strdup(icon);
            existing_item->summary = g_strdup(summary);
            existing_item->body = g_strdup(body);
            existing_item->timestamp = time(NULL);
        }

        g_free(existing_notification->app_name);
        existing_notification->app_name = g_strdup(app_name);
        update_notification_content(existing_notification, icon, summary, body);

        if (existing_notification->timeout_source > 0) {
            g_source_remove(existing_notification->timeout_source);
        }
        if (timeout <= 0) timeout = DEFAULT_TIMEOUT;
        existing_notification->timeout_source = g_timeout_add(timeout, on_timeout, GUINT_TO_POINTER(existing_notification->id));
        emit_history_updated_signal(NULL);
        return existing_notification->id;
    }

    VenomNotification *n = g_new0(VenomNotification, 1);
    n->id = id_counter++;
    n->app_name = g_strdup(app_name);
    n->icon_path = g_strdup(icon);

    // إنشاء النافذة
    n->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(n->win), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(n->win), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(n->win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(n->win), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(n->win), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_default_size(GTK_WINDOW(n->win), NOTIFY_WIDTH, -1);
    gtk_widget_set_app_paintable(n->win, TRUE);
    gtk_widget_set_size_request(n->win, NOTIFY_WIDTH, -1);

    if (notify_use_layer_shell) {
        gtk_layer_init_for_window(GTK_WINDOW(n->win));
        gtk_layer_set_namespace(GTK_WINDOW(n->win), "venom-notify");
        gtk_layer_set_layer(GTK_WINDOW(n->win), GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(n->win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_anchor(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(n->win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    }
    
    // تفعيل الشفافية
    GdkScreen *screen = gtk_widget_get_screen(n->win);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(n->win, visual);
    
    // ربط CSS بالنافذة
    GtkStyleContext *context = gtk_widget_get_style_context(n->win);
    gtk_style_context_add_class(context, "venom-notify");

    // تخطيط العناصر
    GtkWidget *panel_surface = gtk_event_box_new();
    GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_app_paintable(panel_surface, TRUE);
    g_signal_connect(panel_surface, "draw", G_CALLBACK(draw_notification_background), NULL);
    gtk_container_set_border_width(GTK_CONTAINER(main_hbox), 12);
    gtk_widget_set_size_request(main_hbox, NOTIFY_WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(panel_surface), main_hbox);
    gtk_container_add(GTK_CONTAINER(n->win), panel_surface);

    // 1. الأيقونة
    GtkWidget *icon_img = NULL;
    if (icon && strlen(icon) > 0) {
        if (g_path_is_absolute(icon) || g_file_test(icon, G_FILE_TEST_EXISTS)) {
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(icon, 48, 48, TRUE, NULL);
            if (pixbuf) {
                icon_img = gtk_image_new_from_pixbuf(pixbuf);
                g_object_unref(pixbuf);
            }
        } else {
            icon_img = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG);
            gtk_image_set_pixel_size(GTK_IMAGE(icon_img), 48);
        }
    }
    if (!icon_img) icon_img = gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_DIALOG);
    n->icon_img = icon_img;
    
    gtk_box_pack_start(GTK_BOX(main_hbox), icon_img, FALSE, FALSE, 0);
    gtk_widget_set_valign(icon_img, GTK_ALIGN_START);

    // 2. صندوق النصوص والأزرار
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(vbox, NOTIFY_WIDTH - 96, -1);
    gtk_box_pack_start(GTK_BOX(main_hbox), vbox, TRUE, TRUE, 0);

    // العنوان
    GtkWidget *title_lbl = gtk_label_new(summary);
    n->title_lbl = title_lbl;
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(title_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(title_lbl), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(title_lbl), 32);
    gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl), "notify-title");
    gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

    // المحتوى
    GtkWidget *body_lbl = gtk_label_new(NULL);
    n->body_lbl = body_lbl;
    if (body && strlen(body) > 0) {
        // تفعيل قراءة وسوم Pango (مثل البولد والروابط) التي ترسلها بعض التطبيقات
        // التحقق مما إذا كان النص يحتوي على وسوم صالحة
        GError *error = NULL;
        if (pango_parse_markup(body, -1, 0, NULL, NULL, NULL, &error)) {
            // التنسيق سليم، اعرضه مع دعم الخط العريض والروابط
            gtk_label_set_markup(GTK_LABEL(body_lbl), body);
        } else {
            // التنسيق مكسور (يحتوي رموز مثل < أو &)، تراجع واعرضه كنص عادي
            gtk_label_set_text(GTK_LABEL(body_lbl), body);
            g_clear_error(&error);
        }
        gtk_label_set_xalign(GTK_LABEL(body_lbl), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(body_lbl), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(body_lbl), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(body_lbl), 32);
    } else {
        gtk_widget_hide(body_lbl);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(body_lbl), "notify-body");
    gtk_box_pack_start(GTK_BOX(vbox), body_lbl, FALSE, FALSE, 0);

    // 3. الأزرار (Actions)
    if (actions && g_variant_n_children(actions) > 0) {
        GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 4);

        GVariantIter iter;
        gchar *action_key, *action_label;
        g_variant_iter_init(&iter, actions);
        
        while (g_variant_iter_next(&iter, "s", &action_key) && g_variant_iter_next(&iter, "s", &action_label)) {
            // تجاهل زر الـ default (هو عادة مخصص للنقر على مساحة الإشعار نفسها)
            if (g_strcmp0(action_key, "default") == 0) {
                g_free(action_key); g_free(action_label);
                continue;
            }

            GtkWidget *btn = gtk_button_new_with_label(action_label);
            gtk_box_pack_end(GTK_BOX(btn_box), btn, FALSE, FALSE, 0);

            ActionData *data = g_new0(ActionData, 1);
            data->notification_id = n->id;
            data->action_key = g_strdup(action_key);

            g_signal_connect_data(btn, "clicked", G_CALLBACK(on_action_clicked), data, free_action_data, 0);
            
            g_free(action_key); g_free(action_label);
        }
    }

    gtk_widget_show_all(n->win);
    gtk_window_resize(GTK_WINDOW(n->win), NOTIFY_WIDTH, 1);
    
    active_notifications = g_list_append(active_notifications, n);
    
    // إضافة الإشعار للسجل
    add_to_history(n->id, app_name, icon, summary, body);
    
    reposition_notifications();
    
    // إرسال إشارة تحديث السجل
    emit_history_updated_signal(NULL);

    if (timeout <= 0) timeout = DEFAULT_TIMEOUT;
    n->timeout_source = g_timeout_add(timeout, on_timeout, GUINT_TO_POINTER(n->id));
    return n->id;
}

// --- معالجة اتصالات D-Bus ---
static void handle_method_call(GDBusConnection *connection, const gchar *sender,
                               const gchar *object_path, const gchar *interface_name,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data) {
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;
    if (g_strcmp0(method_name, "Notify") == 0) {
        gchar *app_name, *app_icon, *summary, *body;
        guint32 replaces_id;
        GVariant *actions = NULL;
        GVariant *hints = NULL;
        gint32 expire_timeout;

        g_variant_get(parameters, "(susss@as@a{sv}i)", 
                      &app_name, &replaces_id, &app_icon, 
                      &summary, &body, &actions, &hints, &expire_timeout);

        guint32 notification_id = create_new_notification(app_name, replaces_id, summary, body, app_icon, actions, expire_timeout);
        
        // إخبار الكونترول سنتر بتحديث السجل
        emit_history_updated_signal(connection);
        
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", notification_id));

        g_free(app_name); g_free(app_icon); g_free(summary); g_free(body);
        if (actions) g_variant_unref(actions);
        if (hints) g_variant_unref(hints);
    }
    // طلب السجل (للكونترول سنتر)
    else if (g_strcmp0(method_name, "GetHistory") == 0) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ussss)"));
        
        for (GList *l = history_list; l != NULL; l = l->next) {
            NotificationItem *item = (NotificationItem *)l->data;
            g_variant_builder_add(builder, "(ussss)", 
                                  item->id, 
                                  item->app_name ? item->app_name : "",
                                  item->icon_path ? item->icon_path : "",
                                  item->summary ? item->summary : "",
                                  item->body ? item->body : "");
        }
        
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ussss))", builder));
        g_variant_builder_unref(builder);
    }
    // مسح السجل
    else if (g_strcmp0(method_name, "ClearHistory") == 0) {
        clear_history();
        emit_history_updated_signal(connection);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // تفعيل/تعطيل وضع عدم الإزعاج
    else if (g_strcmp0(method_name, "SetDoNotDisturb") == 0) {
        gboolean enabled;
        g_variant_get(parameters, "(b)", &enabled);
        do_not_disturb = enabled;
        
        // إرسال إشارة التغيير
        g_dbus_connection_emit_signal(connection,
                                      NULL,
                                      "/org/freedesktop/Notifications",
                                      "org.venom.NotificationHistory",
                                      "DoNotDisturbChanged",
                                      g_variant_new("(b)", do_not_disturb), NULL);
        
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
    // الحصول على حالة وضع عدم الإزعاج
    else if (g_strcmp0(method_name, "GetDoNotDisturb") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", do_not_disturb));
    }
    else if (g_strcmp0(method_name, "GetCapabilities") == 0) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
        g_variant_builder_add(builder, "s", "body");
        g_variant_builder_add(builder, "s", "actions");
        g_variant_builder_add(builder, "s", "body-markup");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", builder));
        g_variant_builder_unref(builder);
    }
    else if (g_strcmp0(method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ssss)", "Venom", "Vaxp", "1.0", "1.2"));
    }
    else if (g_strcmp0(method_name, "CloseNotification") == 0) {
        guint32 id;
        g_variant_get(parameters, "(u)", &id);
        close_notification(id, 3);
        g_dbus_method_invocation_return_value(invocation, NULL);
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    (void)name;
    (void)user_data;
    dbus_connection = connection;
    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                      node_info->interfaces[0], &interface_vtable, NULL, NULL, NULL);
    g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                      node_info->interfaces[1], &interface_vtable, NULL, NULL, NULL);
}

#include "notify.h"

void notify_init(void) {
    notify_use_layer_shell = notify_is_wayland_session() && gtk_layer_is_supported();
    load_css(); // تحميل الاستايل

    g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
                   G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, NULL, NULL, NULL, NULL);
}
