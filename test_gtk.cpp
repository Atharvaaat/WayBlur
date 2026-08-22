#include <gtk/gtk.h>
static void on_activate(GtkApplication* app, gpointer user_data) {
    GtkWidget* win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "Test");
    
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "window { background: transparent; }", -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);
    
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);
    gtk_widget_set_visible(win, TRUE);
}
int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("org.test.Test", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
