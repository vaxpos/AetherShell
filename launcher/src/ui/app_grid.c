#include "app_grid.h"
#include "app_icon.h"
#include "../core/app_entry.h"
#include "../utils/string_utils.h"

#include <string.h>
#include <math.h>

/* Wrapper to avoid -Wcast-function-type with gtk_widget_destroy */
static void
destroy_widget (gpointer widget, gpointer user_data)
{
    (void) user_data;
    gtk_widget_destroy (GTK_WIDGET (widget));
}

/* -------------------------------------------------------------------------
 * Widget struct
 * ------------------------------------------------------------------------- */

struct _VenomAppGrid {
    GtkBox      parent_instance;

    GPtrArray  *all_apps;       /* full list, not owned */
    GPtrArray  *filtered_apps;  /* subset after filter  */

    int         current_page;
    int         total_pages;

    /* Child widgets */
    GtkWidget  *stack;          /* container for page animation */
    GtkWidget  *flow_box_1;     /* alternates with flow_box_2 */
    GtkWidget  *flow_box_2;
    int         active_box;     /* 1 or 2 */

    GtkWidget  *dots_box;
};

G_DEFINE_TYPE (VenomAppGrid, venom_app_grid, GTK_TYPE_BOX)

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void
update_dots (VenomAppGrid *self)
{
    /* Remove all existing dot children */
    GList *children = gtk_container_get_children (GTK_CONTAINER (self->dots_box));
    g_list_foreach (children, destroy_widget, NULL);
    g_list_free (children);

    for (int i = 0; i < self->total_pages; i++) {
        GtkWidget *dot = gtk_label_new ("●");
        GtkStyleContext *ctx = gtk_widget_get_style_context (dot);
        gtk_style_context_add_class (ctx, "page-dot");

        if (i == self->current_page) {
            gtk_style_context_add_class (ctx, "active");
            /* Force style with an inline provider to work around GTK3 refresh issues */
            GtkCssProvider *prov = gtk_css_provider_new ();
            gtk_css_provider_load_from_data (prov,
                "label { color: rgba(255,255,255,0.90); font-size: 11px; }", -1, NULL);
            gtk_style_context_add_provider (ctx,
                GTK_STYLE_PROVIDER (prov),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
            g_object_unref (prov);
        } else {
            GtkCssProvider *prov = gtk_css_provider_new ();
            gtk_css_provider_load_from_data (prov,
                "label { color: rgba(255,255,255,0.30); font-size: 9px; }", -1, NULL);
            gtk_style_context_add_provider (ctx,
                GTK_STYLE_PROVIDER (prov),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
            g_object_unref (prov);
        }

        gtk_box_pack_start (GTK_BOX (self->dots_box), dot, FALSE, FALSE, 0);
    }

    gtk_widget_show_all (self->dots_box);
}

static void
populate_page (VenomAppGrid *self, GtkStackTransitionType transition)
{
    /* Toggle active box */
    self->active_box = (self->active_box == 1) ? 2 : 1;
    GtkWidget *target_box = (self->active_box == 1) ? self->flow_box_1 : self->flow_box_2;
    const char *target_name = (self->active_box == 1) ? "page1" : "page2";

    /* Clear target box */
    GList *children = gtk_container_get_children (GTK_CONTAINER (target_box));
    g_list_foreach (children, destroy_widget, NULL);
    g_list_free (children);

    int start = self->current_page * APPS_PER_PAGE;
    int end   = MIN (start + APPS_PER_PAGE, (int) self->filtered_apps->len);

    for (int i = start; i < end; i++) {
        AppEntry  *entry = g_ptr_array_index (self->filtered_apps, i);
        GtkWidget *icon  = venom_app_icon_new (entry);
        gtk_container_add (GTK_CONTAINER (target_box), icon);
    }

    gtk_widget_show_all (target_box);

    if (transition != GTK_STACK_TRANSITION_TYPE_NONE) {
        gtk_stack_set_transition_type (GTK_STACK (self->stack), transition);
    }
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), target_name);

    update_dots (self);
}

static void
rebuild_filter (VenomAppGrid *self, const char *query)
{
    g_ptr_array_set_size (self->filtered_apps, 0);

    bool empty = (!query || *query == '\0');

    for (guint i = 0; i < self->all_apps->len; i++) {
        AppEntry *e = g_ptr_array_index (self->all_apps, i);

        if (empty ||
            str_contains_icase (e->name,       query) ||
            str_contains_icase (e->comment,    query) ||
            str_contains_icase (e->categories, query))
        {
            g_ptr_array_add (self->filtered_apps, e);
        }
    }

    self->current_page = 0;
    self->total_pages  = MAX (1,
        (int) ceil ((double) self->filtered_apps->len / APPS_PER_PAGE));

    /* Disable animation on absolute filter change */
    populate_page (self, GTK_STACK_TRANSITION_TYPE_NONE);
}

/* -------------------------------------------------------------------------
 * Signal handlers
 * ------------------------------------------------------------------------- */

static void
on_prev_clicked (GtkButton *btn, gpointer data)
{
    (void) btn;
    VenomAppGrid *self = VENOM_APP_GRID (data);
    if (self->current_page > 0) {
        self->current_page--;
        populate_page (self, GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
    }
}

static void
on_next_clicked (GtkButton *btn, gpointer data)
{
    (void) btn;
    VenomAppGrid *self = VENOM_APP_GRID (data);
    if (self->current_page < self->total_pages - 1) {
        self->current_page++;
        populate_page (self, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
    }
}

/* -------------------------------------------------------------------------
 * GObject class init
 * ------------------------------------------------------------------------- */

static void
venom_app_grid_finalize (GObject *obj)
{
    VenomAppGrid *self = VENOM_APP_GRID (obj);
    g_ptr_array_unref (self->filtered_apps);
    G_OBJECT_CLASS (venom_app_grid_parent_class)->finalize (obj);
}

static void
venom_app_grid_class_init (VenomAppGridClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    obj_class->finalize = venom_app_grid_finalize;
}

static GtkWidget *
create_flow_box (void)
{
    GtkWidget *fb = gtk_flow_box_new ();
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (fb), GRID_COLUMNS);
    gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (fb), GRID_COLUMNS);
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (fb), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (fb), TRUE);
    gtk_widget_set_vexpand (fb, TRUE);
    gtk_widget_set_hexpand (fb, TRUE);

    gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (fb), 10);
    gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (fb), 10);
    gtk_widget_set_valign (fb, GTK_ALIGN_START); /* Prevent jumpiness during transitions */

    gtk_style_context_add_class (gtk_widget_get_style_context (fb), "app-grid");
    return fb;
}

static void
venom_app_grid_init (VenomAppGrid *self)
{
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                    GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing (GTK_BOX (self), 20);

    /* ── Main Layout ─────────────────────────────────────────────────── */
    /*
     * Top struct: VBox
     *  - Middle: HBox (Left Arrow, FlowBox, Right Arrow)
     *  - Bottom: Dots box (centered)
     */

    /* Middle HBox */
    GtkWidget *middle_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_start (GTK_BOX (self), middle_hbox, TRUE, TRUE, 0);

    /* ── Stack (replaces Single FlowBox) ────────────────────────────── */
    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_duration (GTK_STACK (self->stack), 350);
    gtk_widget_set_vexpand (self->stack, TRUE);
    gtk_widget_set_hexpand (self->stack, TRUE);

    self->flow_box_1 = create_flow_box ();
    self->flow_box_2 = create_flow_box ();
    self->active_box = 1;

    gtk_stack_add_named (GTK_STACK (self->stack), self->flow_box_1, "page1");
    gtk_stack_add_named (GTK_STACK (self->stack), self->flow_box_2, "page2");

    gtk_box_pack_start (GTK_BOX (middle_hbox), self->stack, TRUE, TRUE, 0);

    /* ── Bottom Pagination Dots ──────────────────────────────────────── */
    GtkWidget *dots_container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign (dots_container, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom (dots_container, 40);
    gtk_box_pack_end (GTK_BOX (self), dots_container, FALSE, FALSE, 0);

    self->dots_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_valign (self->dots_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (dots_container), self->dots_box, FALSE, FALSE, 0);

    self->filtered_apps = g_ptr_array_new ();
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GtkWidget *
venom_app_grid_new (GPtrArray *apps)
{
    VenomAppGrid *self = g_object_new (VENOM_TYPE_APP_GRID, NULL);
    self->all_apps = apps;
    rebuild_filter (self, NULL);
    return GTK_WIDGET (self);
}

void
venom_app_grid_set_filter (VenomAppGrid *grid, const char *query)
{
    g_return_if_fail (VENOM_IS_APP_GRID (grid));
    rebuild_filter (grid, query);
}

void
venom_app_grid_go_next_page (VenomAppGrid *grid)
{
    g_return_if_fail (VENOM_IS_APP_GRID (grid));
    on_next_clicked (NULL, grid);
}

void
venom_app_grid_go_prev_page (VenomAppGrid *grid)
{
    g_return_if_fail (VENOM_IS_APP_GRID (grid));
    on_prev_clicked (NULL, grid);
}
