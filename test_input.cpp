#include <gtk/gtk.h>
int main(int argc, char** argv) {
    gtk_init();
    GtkWidget* win = gtk_window_new();
    gtk_widget_realize(win);
    GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(win));
    cairo_region_t* region = cairo_region_create();
    gdk_surface_set_input_region(surface, region);
    cairo_region_destroy(region);
    return 0;
}
