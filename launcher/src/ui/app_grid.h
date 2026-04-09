#pragma once

#include <gtk/gtk.h>
#include <glib.h>

G_BEGIN_DECLS

#define VENOM_TYPE_APP_GRID (venom_app_grid_get_type ())
G_DECLARE_FINAL_TYPE (VenomAppGrid, venom_app_grid, VENOM, APP_GRID, GtkBox)

/**
 * VenomAppGrid - Paginated icon grid + pagination dots.
 * Lays out AppEntry list in pages of APPS_PER_PAGE items.
 */
#define APPS_PER_PAGE   35
#define GRID_COLUMNS     7

GtkWidget *venom_app_grid_new          (GPtrArray  *apps);
void       venom_app_grid_set_filter   (VenomAppGrid *grid,
                                        const char   *query);
void       venom_app_grid_go_next_page (VenomAppGrid *grid);
void       venom_app_grid_go_prev_page (VenomAppGrid *grid);

G_END_DECLS
