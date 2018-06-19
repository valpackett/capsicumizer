#include <errno.h>
#include <sys/capsicum.h>
#include <gtk/gtk.h>
#include "capsicumizer.h"

static void activate(GtkApplication* app, gpointer user_data) {
	GtkWidget *window = gtk_application_window_new (app);
	gtk_window_set_title(GTK_WINDOW(window), "Window");
	gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);
	gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
	/* capsicumize_dir("/usr/local/share"); */
	/* capsicumize_dir("/usr/local/lib"); */
	/* capsicumize_dir("/var/db/fontconfig"); */
	/* capsicumize_dir("/usr/share"); */
	/* capsicumize_dir("/home/greg/.config"); */
	/* capsicumize_dir("/home/greg/.local/share"); */
	/* capsicumize_dir("/tmp"); */
	/* capsicumize_shm("gdk-wayland"); */
	/* if (cap_enter() != 0) return errno; */
	GtkApplication *app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
