#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>


struct pkg_header {
	char ident[4];
	uint8_t ver[2];
	uint8_t date[6];
	uint32_t c_size;
	uint8_t md5[16];
};

struct infomation {
	GMutex data_mutex;
	GCond data_cond;
	int dirty;
	char info[256];
};

struct update_step {
	char *info;
	char *cmd;
};


GtkWidget *main_window, *detail_view, *scrolled_window,
			*open_button, *update_button, *progress_bar;

struct infomation update_info = {
	.dirty = 0,
};

struct update_step step[] = {
	[0] = { .info = "正在上传升级数据", .cmd = "./upload" },
	[1] = { .info = "正在执行升级", .cmd = "./perform" },
};

int oper_cnt, progress;


void destroy(GtkWidget *window, gpointer data)
{
	gtk_main_quit();
}

gboolean scroll_to_bottom(gpointer data)
{
	GtkAdjustment *adj;
	gdouble upper;

	adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
	upper = gtk_adjustment_get_upper(adj);
	gtk_adjustment_set_value(adj, upper);

	return FALSE;
}

gboolean display_info(gpointer data)
{
	GtkTextBuffer *buffer;
	GtkTextIter end;
	struct infomation *pinfo = (struct infomation *)data;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(detail_view));
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert(buffer, &end, pinfo->info, -1);
	gtk_text_buffer_insert(buffer, &end, "\n", -1);

	g_idle_add(scroll_to_bottom, NULL);

	g_mutex_lock(&pinfo->data_mutex);
	pinfo->dirty = 0;
	g_cond_signal(&pinfo->data_cond);
	g_mutex_unlock(&pinfo->data_mutex);

	return FALSE;
}

void thread_print_info(char *info)
{
	g_mutex_lock(&update_info.data_mutex);

	if (update_info.dirty) {
		while (update_info.dirty)
			g_cond_wait(&update_info.data_cond, &update_info.data_mutex);
	}

	update_info.dirty = 1;

	strcpy(update_info.info, info);

	g_idle_add(display_info, &update_info);

	g_mutex_unlock(&update_info.data_mutex);
}

void print_info(gchar *info)
{
	GtkTextBuffer *buffer;
	GtkTextIter end;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(detail_view));
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert(buffer, &end, info, -1);
	gtk_text_buffer_insert(buffer, &end, "\n", -1);

	g_idle_add(scroll_to_bottom, NULL);
}

void clear_info()
{
	GtkTextBuffer *buffer;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(detail_view));
	gtk_text_buffer_set_text(buffer, "", -1);
}

int open_update_package(char *filename, struct pkg_header *header)
{
	FILE *fpkg, *fc;
	int n;
	uint8_t buf[100];

	fpkg = fopen(filename, "rb");
	if (fpkg == NULL)
		goto _err1;

	if (fread(header, sizeof(struct pkg_header), 1, fpkg) != 1)
		goto _err2;

	if (strncmp(header->ident, "*PKG", 4) != 0)
		goto _err2;

	fc = fopen("content.tar.gz", "w+b");
	if (fc == NULL)
		goto _err2;

	rewind(fpkg);
	fseek(fpkg, sizeof(struct pkg_header), SEEK_SET);

	do {
		n = read(fileno(fpkg), buf, 100);
		if (n < 0)
			goto _err3;

		if (write(fileno(fc), buf, n) < 0)
			goto _err3;
	} while (n == 100);

	fclose(fc);
	fclose(fpkg);
	return 0;

_err3:
	fclose(fc);
_err2:
	fclose(fpkg);
_err1:
	return -1;

}

void open_clicked(GtkButton *button, GtkWindow *window)
{
	GtkWidget *dialog;
	char work_dir[PATH_MAX], str[256];
	struct pkg_header header;
	gchar *filename;

	dialog = gtk_file_chooser_dialog_new("打开 ...", window,
										GTK_FILE_CHOOSER_ACTION_OPEN,
										GTK_STOCK_CANCEL,
										GTK_RESPONSE_CANCEL,
										GTK_STOCK_OPEN,
										GTK_RESPONSE_ACCEPT,
										NULL);

	getcwd(work_dir, PATH_MAX);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), work_dir);

	gint result = gtk_dialog_run(GTK_DIALOG (dialog));
	if (result == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		clear_info();

		if (open_update_package(filename, &header) == 0) {
			sprintf(str, "升级包版本 V%d.%d", header.ver[0], header.ver[1]);
			print_info(str);

			sprintf(str, "升级包创建时间 %02d/%02d/%02d %02d:%02d:%02d",
					header.date[0], header.date[1], header.date[2],
					header.date[3], header.date[4], header.date[5]);
			print_info(str);

			sprintf(str, "升级包大小 %d字节\n", header.c_size);
			print_info(str);

			gtk_widget_set_sensitive(update_button, TRUE);
		} else
			print_info("无效的文件！");
	}

	gtk_widget_destroy (dialog);
}

gboolean finish_up_update(gpointer data)
{
	gtk_widget_set_sensitive(open_button, TRUE);
	gtk_widget_set_sensitive(update_button, TRUE);
	return FALSE;
}

gboolean update_progress_bar(gpointer data)
{
	gdouble f = (gdouble)progress/(gdouble)oper_cnt;
	char buf[50];

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), f);
	sprintf(buf, "%d%%", (int)(f*100));
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), buf);

	return FALSE;
}

gpointer update_thread(gpointer data)
{
	int i;

	for (i = 0;  i < sizeof(step)/sizeof(struct update_step); i++) {
		thread_print_info(step[i].info);

		if (system(step[i].cmd) != 0) {
			thread_print_info("升级失败！\n");
			goto _finish;
		}

		progress = i+1;
		g_idle_add(update_progress_bar, NULL);
	}

	thread_print_info("升级成功！\n");

_finish:
	g_idle_add(finish_up_update, NULL);
	return NULL;
}

void update_clicked(GtkButton *button, GtkWindow *window)
{
	gtk_widget_set_sensitive(open_button, FALSE);
	gtk_widget_set_sensitive(update_button, FALSE);

	oper_cnt = sizeof(step)/sizeof(struct update_step);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), "0%");

	g_thread_new("update thread", update_thread, NULL);
}

int main(int argc, char *argv[])
{
	GtkBuilder *builder;

	gtk_init(&argc, &argv);

	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "gui.glade", NULL);

	main_window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
	detail_view = GTK_WIDGET(gtk_builder_get_object(builder, "detail_view"));
	scrolled_window = GTK_WIDGET(gtk_builder_get_object(builder, "scrolledwindow"));
	open_button = GTK_WIDGET(gtk_builder_get_object(builder, "open_button"));
	update_button = GTK_WIDGET(gtk_builder_get_object(builder, "update_button"));
	progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progressbar"));

	GtkWidget *expander = GTK_WIDGET(gtk_builder_get_object(builder, "expander"));
	gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);

	gtk_builder_connect_signals(builder, NULL);

	gtk_widget_show_all(main_window);

	/* Hand control over to the main loop. */
	gtk_main();

	return 0;
}
