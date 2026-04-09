#include "search_bar.h"

/* -------------------------------------------------------------------------
 * Widget struct
 * ------------------------------------------------------------------------- */

struct _VenomSearchBar {
    GtkSearchEntry  parent_instance;
    guint           debounce_id;   /* g_timeout source id */
};

G_DEFINE_TYPE (VenomSearchBar, venom_search_bar, GTK_TYPE_SEARCH_ENTRY)

/* Signals */
enum { SIGNAL_SEARCH_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

/* -------------------------------------------------------------------------
 * Debounce logic
 * ------------------------------------------------------------------------- */

static gboolean
debounce_fire (gpointer user_data)
{
    VenomSearchBar *self = VENOM_SEARCH_BAR (user_data);
    self->debounce_id = 0;
    g_signal_emit (self, signals[SIGNAL_SEARCH_CHANGED], 0);
    return G_SOURCE_REMOVE;
}

static void
on_text_changed (GtkEditable *editable, gpointer user_data)
{
    (void) user_data;
    VenomSearchBar *self = VENOM_SEARCH_BAR (editable);

    /* Cancel pending debounce */
    if (self->debounce_id) {
        g_source_remove (self->debounce_id);
        self->debounce_id = 0;
    }

    /* Schedule new debounce: 150 ms */
    self->debounce_id = g_timeout_add (150, debounce_fire, self);
}

/* -------------------------------------------------------------------------
 * GObject class init
 * ------------------------------------------------------------------------- */

static void
venom_search_bar_class_init (VenomSearchBarClass *klass)
{
    signals[SIGNAL_SEARCH_CHANGED] =
        g_signal_new ("search-changed-debounced",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}

static void
venom_search_bar_init (VenomSearchBar *self)
{
    self->debounce_id = 0;

    gtk_widget_set_name (GTK_WIDGET (self), "venom-search-entry");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (GTK_WIDGET (self)), "search-bar");

    gtk_entry_set_placeholder_text (GTK_ENTRY (self), "Search");
    gtk_entry_set_width_chars (GTK_ENTRY (self), 30);
    gtk_widget_set_halign (GTK_WIDGET (self), GTK_ALIGN_CENTER);

    g_signal_connect (self, "changed",
                      G_CALLBACK (on_text_changed), NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GtkWidget *
venom_search_bar_new (void)
{
    return g_object_new (VENOM_TYPE_SEARCH_BAR, NULL);
}

const char *
venom_search_bar_get_text (VenomSearchBar *bar)
{
    g_return_val_if_fail (VENOM_IS_SEARCH_BAR (bar), "");
    return gtk_entry_get_text (GTK_ENTRY (bar));
}

void
venom_search_bar_clear (VenomSearchBar *bar)
{
    g_return_if_fail (VENOM_IS_SEARCH_BAR (bar));
    gtk_entry_set_text (GTK_ENTRY (bar), "");
}

void
venom_search_bar_grab_focus (VenomSearchBar *bar)
{
    g_return_if_fail (VENOM_IS_SEARCH_BAR (bar));
    gtk_widget_grab_focus (GTK_WIDGET (bar));
}
