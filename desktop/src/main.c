/*
 * main.c
 * vaxp Desktop Manager - Main Entry Point
 * Pro Desktop: Free Positioning, Smart Grid (No Stacking), Recursive Copy, System DnD.
 *
 * Compile with Makefile:
 * make
 */

#include <gtk/gtk.h>
#include <malloc.h>
#include "wallpaper.h"
#include "icons.h"
#include "selection.h"
#include "menu.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* Initialize main window and layout */
    init_main_window();
    
    /* Setup Background as Drag Dest */
    GtkTargetEntry targets[] = { { "text/uri-list", 0, 0 } };
    gtk_drag_dest_set(icon_layout, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_MOVE | GDK_ACTION_COPY);
    g_signal_connect(icon_layout, "drag-data-received", G_CALLBACK(on_bg_drag_data_received), NULL);
    
    /* Connect selection drawing */
    g_signal_connect_after(icon_layout, "draw", G_CALLBACK(on_layout_draw_fg), NULL);
    
    /* Connect background events */
    g_signal_connect(icon_layout, "button-press-event", G_CALLBACK(on_bg_button_press), NULL);
    g_signal_connect(icon_layout, "motion-notify-event", G_CALLBACK(on_bg_motion), NULL);
    g_signal_connect(icon_layout, "button-release-event", G_CALLBACK(on_bg_button_release), NULL);

    /* Apply CSS styling */
    GdkScreen *screen = gtk_widget_get_screen(main_window);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, 
        "window { background-color: transparent; }"
        "#desktop-item { background: transparent; border-radius: 5px; padding: 8px; transition: all 0.1s; }"
        "#desktop-item:hover { background: rgba(0, 0, 0, 0.15); }"
        "#desktop-item.selected { background: rgba(52, 152, 219, 0.4); border: 1px solid rgba(52, 152, 219, 0.8); }"
        "label { color: white; text-shadow: 1px 1px 2px black; font-weight: bold; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), 800);

    /* Load desktop icons */
    refresh_icons();

    /* Show window and start main loop */
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(main_window);
    malloc_trim(0);
    gtk_main();

    return 0;
}
