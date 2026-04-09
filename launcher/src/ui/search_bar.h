#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define VENOM_TYPE_SEARCH_BAR (venom_search_bar_get_type ())
G_DECLARE_FINAL_TYPE (VenomSearchBar, venom_search_bar,
                      VENOM, SEARCH_BAR, GtkSearchEntry)

/**
 * VenomSearchBar - Styled search entry with debounce.
 * Emits "search-changed" signal after 150ms debounce.
 */
GtkWidget  *venom_search_bar_new         (void);
const char *venom_search_bar_get_text    (VenomSearchBar *bar);
void        venom_search_bar_clear       (VenomSearchBar *bar);
void        venom_search_bar_grab_focus  (VenomSearchBar *bar);

G_END_DECLS
