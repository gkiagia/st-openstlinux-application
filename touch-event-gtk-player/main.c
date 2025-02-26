/*
 * Copyright (C) 2014-2015 Collabora Ltd.
 *   @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 * Copyright (C) 2019, STMicroelectronics - All Rights Reserved
 *   @author Christophe Priouzeau <christophe.priouzeau@st.com>
 *
 * SPDX-License-Identufier: GPL-2.0+
 *
 * NOTE: inspirated from https://github.com/GStreamer/gst-plugins-bad/tree/master/tests/examples/waylandsink
 */

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#else
#error "Wayland is not supported in GTK+"
#endif

#include <gst/video/videooverlay.h>
#include <wayland/wayland.h> // for gst/wayland/wayland.h

gboolean local_kb_set_key_handler(gpointer user_data);

static gchar *graph = NULL;
static gchar *shader_file = NULL;
static gboolean nofullscreen = FALSE;
static guint32 last_touch_tap = 0;
static guint32 last_pointer_tap = 0;

static GMainLoop *loop;

static GOptionEntry entries[] = {
	{"No Fullscreen", 'F', 0, G_OPTION_ARG_NONE, &nofullscreen,
		"Do not put video on fullscreeen", NULL},
	{"graph", 0, 0, G_OPTION_ARG_STRING, &graph, "Gstreamer graph to use", NULL},
	{"shader", 0, 0, G_OPTION_ARG_STRING, &shader_file, "Gstreamer shader graph to use", NULL},

	{NULL}
};

typedef struct
{
	GtkWidget *window_widget;
	GtkWidget *video_widget;
	GtkWidget *box_widget;

	GstElement *pipeline;
	GstVideoOverlay *overlay;

	gchar **argv;
	gint current_uri;             /* index for argv */
	gboolean to_start;
	gint video_width;
	gint video_height;
} DemoApp;

static void
on_about_to_finish (GstElement * playbin, DemoApp * d)
{
	if (d->argv[++d->current_uri] == NULL)
		d->current_uri = 1;

	g_print ("Now playing %s\n", d->argv[d->current_uri]);
	g_object_set (playbin, "uri", d->argv[d->current_uri], NULL);
}

// ---------------------------------------------------
// ---------------------------------------------------
struct _gtk_wayland_for_event {
	DemoApp *d;

	struct wl_display *wl_display;
	struct wl_registry *wl_registry;
	struct wl_seat *wl_seat;
	struct wl_pointer *wl_pointer;
	struct wl_touch *wl_touch;
};
struct _gtk_wayland_for_event wayland_support;

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface,
		     wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface)
{
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
		      uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		      uint32_t serial, uint32_t time, uint32_t button,
		      uint32_t state)
{
	struct _gtk_wayland_for_event* this = (struct _gtk_wayland_for_event*) data;
	guint32 diff;
	GstState actual_state;

	//g_print("Pointer button %d \n", state);

	if (WL_POINTER_BUTTON_STATE_RELEASED == state) return;

	if (last_pointer_tap == 0) {
		last_pointer_tap = time;
		//g_print("--> SIMPLE TAP\n");
		gst_element_get_state(this->d->pipeline, &actual_state, NULL, -1);
		if (actual_state == GST_STATE_PAUSED)
			gst_element_set_state (this->d->pipeline, GST_STATE_PLAYING);
		else
			gst_element_set_state (this->d->pipeline, GST_STATE_PAUSED);
	} else {
		diff = time - last_pointer_tap;
		if (last_pointer_tap != 0) {
			last_pointer_tap = time;
			if (diff < 600) {
				//g_print("--> DOUBLE TAP\n");
				gst_element_set_state (this->d->pipeline, GST_STATE_NULL);
				g_main_loop_quit (loop);
				exit(1);
				last_pointer_tap = 0;
			} else {
				gst_element_get_state(this->d->pipeline, &actual_state, NULL, -1);
				if (actual_state == GST_STATE_PAUSED)
					gst_element_set_state (this->d->pipeline, GST_STATE_PLAYING);
				else
					gst_element_set_state (this->d->pipeline, GST_STATE_PAUSED);
				//g_print("--> SIMPLE TAP\n");
			}
			//g_print("--> BEGIN diff = %d\n", diff);
		}
	}
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		    uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};
static void
touch_handle_down(void *data, struct wl_touch *wl_touch,
	uint32_t serial, uint32_t time, struct wl_surface *surface,
	int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct _gtk_wayland_for_event* this = (struct _gtk_wayland_for_event*) data;
	guint32 diff;
	GstState actual_state;

	g_print("TOUCH down\n");
	if (last_touch_tap == 0) {
		last_touch_tap = time;
		gst_element_get_state(this->d->pipeline, &actual_state, NULL, -1);
		if (actual_state == GST_STATE_PAUSED)
			gst_element_set_state (this->d->pipeline, GST_STATE_PLAYING);
		else
			gst_element_set_state (this->d->pipeline, GST_STATE_PAUSED);
	} else {
		diff = time - last_touch_tap;
		if (last_touch_tap != 0) {
			last_touch_tap = time;
			if (diff < 600) {
				//g_print("--> DOUBLE TAP\n");
				gst_element_set_state (this->d->pipeline, GST_STATE_NULL);
				g_main_loop_quit (loop);
				exit(1); //force to quit application
			} else {
				gst_element_get_state(this->d->pipeline, &actual_state, NULL, -1);
				if (actual_state == GST_STATE_PAUSED)
					gst_element_set_state (this->d->pipeline, GST_STATE_PLAYING);
				else
					gst_element_set_state (this->d->pipeline, GST_STATE_PAUSED);
				//g_print("--> SIMPLE TAP\n");
			}
			//g_print("--> BEGIN diff = %d\n", diff);
		}
	}
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
		uint32_t serial, uint32_t time, int32_t id)
{
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
		    uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
}

static void
touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
}
static void
touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
}
static const struct wl_touch_listener touch_listener = {
	touch_handle_down,
	touch_handle_up,
	touch_handle_motion,
	touch_handle_frame,
	touch_handle_cancel,
};
static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
			 enum wl_seat_capability caps)
{
	struct _gtk_wayland_for_event* this = (struct _gtk_wayland_for_event*) data;

	//g_print("seat_handle_capabilities %d\n", caps);

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !this->wl_pointer) {
		this->wl_pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(this->wl_pointer, &pointer_listener, this);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && this->wl_pointer) {
		wl_pointer_destroy(this->wl_pointer);
		this->wl_pointer = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !this->wl_touch) {
		this->wl_touch = wl_seat_get_touch(seat);
		wl_touch_set_user_data(this->wl_touch, this);
		wl_touch_add_listener(this->wl_touch, &touch_listener, this);
	} else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && this->wl_touch) {
		wl_touch_destroy(this->wl_touch);
		this->wl_touch = NULL;
	}
}

static const struct wl_seat_listener seat_listener =
{
	seat_handle_capabilities,
};
static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
			const char *interface, uint32_t version)
{
	struct _gtk_wayland_for_event* this = (struct _gtk_wayland_for_event*) data;

	//g_print("registry event for %s id, %d data %p\n", interface, id, data);
	if (strcmp(interface, "wl_seat") == 0) {
		this->wl_seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
		wl_seat_add_listener(this->wl_seat, &seat_listener, this);
	}
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
	//g_print("registry lost for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_remover
};

void init_wl_event(GtkWidget *widget, DemoApp *d) {
	GdkDisplay *display;

	wayland_support.d = d;

	display = gtk_widget_get_display (widget);
	wayland_support.wl_display = gdk_wayland_display_get_wl_display (display);
	if (wayland_support.wl_display == NULL)
		return;
	wayland_support.wl_registry = wl_display_get_registry(wayland_support.wl_display);
	wl_registry_add_listener(wayland_support.wl_registry, &registry_listener, &wayland_support);
	wl_display_dispatch(wayland_support.wl_display);
	wl_display_roundtrip(wayland_support.wl_display);
}
// ---------------------------------------------------
// ---------------------------------------------------
struct _gst_caps_video_size {
	gint width;
	gint height;
};

static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
	gchar *str = gst_value_serialize (value);
	const gchar *sfield = g_quark_to_string (field);
	struct _gst_caps_video_size *size = pfx;
	//g_print("--%s: %s\n", sfield, str);

	if (!strncmp(sfield, "width", 5) && (G_VALUE_TYPE(value) == G_TYPE_INT)) {
		size->width = g_value_get_int (value);
	} else if (!strncmp(sfield, "height", 6) && (G_VALUE_TYPE(value) == G_TYPE_INT)) {
		size->height = g_value_get_int (value);
	}
	g_free (str);
	return TRUE;
}

static gboolean msg_cb(GstBus * bus, GstMessage * message, gpointer data)
{
	g_print ("*******message: %s\n", GST_MESSAGE_TYPE_NAME (message));
	return TRUE;
}
static void
msg_structure_change (DemoApp *d)
{
	const GstStructure *s;
	gint width, height;
	gint i;
	GstElement *sink;
	GstPad *pad = NULL;
	GstCaps *caps = NULL;
	struct _gst_caps_video_size video_size;
	video_size.width = video_size.height = 0;

	sink = gst_bin_get_by_name(GST_BIN(d->pipeline), "waylandsink0");
	pad = gst_element_get_static_pad (GST_ELEMENT(sink), "sink");

	caps = gst_pad_get_current_caps (GST_PAD(pad));
	if (!caps)
		caps = gst_pad_query_caps (GST_PAD(pad), NULL);

	for (i = 0; i < gst_caps_get_size(caps); i++) {
		GstStructure *structure = gst_caps_get_structure(caps, i);
		g_print ("******%s\n", gst_structure_get_name (structure));
		gst_structure_foreach (structure, print_field, &video_size);
	}
	if (nofullscreen) {
		if( (200 >= video_size.width) || (200 >= video_size.height) ) {
			video_size.width = 640;
			video_size.height = 480;
		}
		gtk_widget_set_size_request (d->window_widget, video_size.width, video_size.height);
	}
	g_print("-----------main:set size to %d %d \n", video_size.width, video_size.height );
	d->video_width = video_size.width;
	d->video_height = video_size.height;

	gst_caps_unref (caps);
	gst_object_unref (pad);
}

static void
msg_state_changed (GstBus * bus, GstMessage * message, gpointer user_data)
{
	const GstStructure *s;
	DemoApp *d = user_data;

	s = gst_message_get_structure (message);

	/* We only care about state changed on the pipeline */
	if (s && GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (d->pipeline)) {
		GstState old, new, pending;

		gst_message_parse_state_changed (message, &old, &new, &pending);

		switch (new){
		case GST_STATE_VOID_PENDING:
			g_print("new state: GST_STATE_VOID_PENDING\n");break;
		case GST_STATE_NULL:
			g_print("new state: GST_STATE_NULL\n");break;
		case GST_STATE_READY:
			g_print("new state: GST_STATE_READY\n");
			if (d->to_start)
				gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
			break;
		case GST_STATE_PAUSED:
			g_print("new state: GST_STATE_PAUSED\n");
			break;
		case GST_STATE_PLAYING:
			g_print("new state: GST_STATE_PLAYING\n");
			break;
		default:
			break;
		}
	}
}

int
gstreamer_bus_callback (struct _GstBus * bus, GstMessage * message, void *data)
{
	DemoApp *d = data;

	//g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR:{
		GError *err;
		gchar *debug;

		g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));

		gst_message_parse_error (message, &err, &debug);
		g_print ("Error: %s\n", err->message);
		g_error_free (err);
		if (debug) {
			g_print ("Debug details: %s\n", debug);
			g_free (debug);
		}
		gst_element_set_state (d->pipeline, GST_STATE_NULL);
		break;
	}

	case GST_MESSAGE_STATE_CHANGED:
		g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));
		msg_state_changed (bus, message, data);
		break;

	case GST_MESSAGE_EOS:
		/* end-of-stream */
		g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));
		g_print ("EOS\n");
		gst_element_set_state (d->pipeline, GST_STATE_NULL);
		g_main_loop_quit (loop);
		exit(1);
		break;

	default:
		/* unhandled message */
		break;
	}
	return TRUE;
}



static gboolean
button_notify_event_cb (GtkWidget      *widget,
			GdkEventButton *event,
			gpointer        data)
{
	DemoApp *d = data;
	guint32 diff;
	GstState actual_state;

	if (event->button == GDK_BUTTON_PRIMARY) {
		if (last_touch_tap == 0) {
			last_touch_tap = event->time;
			gst_element_get_state(d->pipeline, &actual_state, NULL, -1);
			if (actual_state == GST_STATE_PAUSED)
				gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
			else
				gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
		} else {
			diff = event->time - last_touch_tap;
			if (last_touch_tap != 0) {
				last_touch_tap = event->time;
				if (diff < 600) {
					//g_print("--> DOUBLE TAP\n");
					gst_element_set_state (d->pipeline, GST_STATE_NULL);
					g_main_loop_quit (loop);
					exit(1); //force to quit application
				} else {
					gst_element_get_state(d->pipeline, &actual_state, NULL, -1);
					if (actual_state == GST_STATE_PAUSED)
						gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
					else
						gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
					//g_print("--> SIMPLE TAP\n");
				}
				//g_print("--> BEGIN diff = %d\n", diff);
			}
		}
	}

	/* We've handled the event, stop processing */
	return TRUE;
}

static gboolean
touch_notify_event_cb (GtkWidget      *widget,
					   GdkEvent *event,
					   gpointer        data)
{
	DemoApp *d = data;
	guint32 diff;
	GstState actual_state;

	g_print("--> %s\n", __FUNCTION__);
	switch(event->touch.type) {
	case GDK_TOUCH_BEGIN:
		if (last_touch_tap == 0) {
			last_touch_tap = event->touch.time;
			gst_element_get_state(d->pipeline, &actual_state, NULL, -1);
			if (actual_state == GST_STATE_PAUSED)
				gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
			else
				gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
		} else {
			diff = event->touch.time - last_touch_tap;
			if (last_touch_tap != 0) {
				last_touch_tap = event->touch.time;
				if (diff < 600) {
					g_print("--> DOUBLE TAP\n");
					gst_element_set_state (d->pipeline, GST_STATE_NULL);
					g_main_loop_quit (loop);
					exit(1); //force to quit application
				} else {
					gst_element_get_state(d->pipeline, &actual_state, NULL, -1);
					if (actual_state == GST_STATE_PAUSED)
						gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
					else
						gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
					g_print("--> SIMPLE TAP\n");
				}
				g_print("--> BEGIN diff = %d\n", diff);
			}
		}
		break;
	case GDK_TOUCH_UPDATE:
		//g_print("--> UPDATE\n");
		break;
	case GDK_TOUCH_END:
		//g_print("--> END\n");
		break;
	case GDK_TOUCH_CANCEL:
		//g_print("--> CANCEL\n");
		break;
	default:
		break;
		//g_print("--> something else \n");
	}
	/* We've handled it, stop processing */
	return TRUE;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, gpointer user_data)
{
	DemoApp *d = user_data;

	if (gst_is_wayland_display_handle_need_context_message (message)) {
		GstContext *context;
		GdkDisplay *display;
		struct wl_display *display_handle;

		msg_structure_change(d);

		display = gtk_widget_get_display (d->video_widget);
		display_handle = gdk_wayland_display_get_wl_display (display);
		context = gst_wayland_display_handle_context_new (display_handle);
		gst_element_set_context (GST_ELEMENT (GST_MESSAGE_SRC (message)), context);

		goto drop;
	} else if (gst_is_video_overlay_prepare_window_handle_message (message)) {
		GtkAllocation allocation;
		GdkWindow *window;
		struct wl_surface *window_handle;

		/* GST_MESSAGE_SRC (message) will be the overlay object that we have to
		 * use. This may be waylandsink, but it may also be playbin. In the latter
		 * case, we must make sure to use playbin instead of waylandsink, because
		 * playbin resets the window handle and render_rectangle after restarting
		 * playback and the actual window size is lost */
		d->overlay = GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (message));

		msg_structure_change(d);

		gtk_widget_get_allocation (d->video_widget, &allocation);
		window = gtk_widget_get_window (d->video_widget);
		window_handle = gdk_wayland_window_get_wl_surface (window);

		init_wl_event(d->video_widget, d);

		g_print ("setting window handle and size (%d x %d)\n",
				 allocation.width, allocation.height);

		gst_video_overlay_set_window_handle (d->overlay, (guintptr) window_handle);
		gst_video_overlay_set_render_rectangle (d->overlay, allocation.x,
						allocation.y, allocation.width, allocation.height);

		if (d->to_start) {
			gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
		}

		goto drop;
	}

	return GST_BUS_PASS;

drop:
	gst_message_unref (message);
	return GST_BUS_DROP;
}

/* We use the "draw" callback to change the size of the sink
 * because the "configure-event" is only sent to top-level widgets. */
static gboolean
video_widget_draw_cb (GtkWidget * widget, cairo_t * cr, gpointer user_data)
{
	DemoApp *d = user_data;
	GtkAllocation allocation;

	gtk_widget_get_allocation (widget, &allocation);

	g_print ("draw_cb x %d, y %d, w %d, h %d\n",
	    allocation.x, allocation.y, allocation.width, allocation.height);

	if (d->overlay) {

//		gst_video_overlay_set_render_rectangle (d->overlay, allocation.x,
//							allocation.y, allocation.width, allocation.height);
		if (nofullscreen) {
			int x, y;
			x = allocation.x + (allocation.width - d->video_width)/2;
			y = allocation.y + (allocation.height - d->video_height)/2;
			gst_video_overlay_set_render_rectangle (d->overlay, x,
								y, d->video_width, d->video_height);
		}
        else
			gst_video_overlay_set_render_rectangle (d->overlay, allocation.x,
							allocation.y, allocation.width, allocation.height);
	}

	/* There is no need to call gst_video_overlay_expose().
	 * The wayland compositor can always re-draw the window
	 * based on its last contents if necessary */

	return FALSE;
}

static void
build_window (DemoApp * d)
{
	GtkWidget *box;

	/* windows */
	d->window_widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW(d->window_widget), "GStreamer Wayland GTK ");
	gtk_window_set_transient_for (GTK_WINDOW(d->window_widget), NULL);
	g_signal_connect (GTK_WINDOW(d->window_widget), "destroy", G_CALLBACK (g_main_loop_quit), loop);
	//if (!nofullscreen)
		gtk_window_fullscreen(GTK_WINDOW(d->window_widget));
	//else {
	//	gtk_window_set_decorated (GTK_WINDOW(d->window_widget), FALSE);
	//}

	/* styling background color to black */
	GtkCssProvider* provider = gtk_css_provider_new();

	char *data = "#transparent_bg,GtkDrawingArea {\n"
			"    background-color: rgba (88%, 88%, 88%, 1.0);\n"
			"}";

	gtk_css_provider_load_from_data(provider, data, -1, NULL);
	gtk_style_context_add_provider(gtk_widget_get_style_context(d->window_widget),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);

	g_object_unref(provider);

	gtk_widget_set_name(d->window_widget, "transparent_bg");


	d->video_widget = gtk_event_box_new ();
	gtk_widget_set_support_multidevice (d->video_widget, TRUE);
	gtk_widget_set_app_paintable (d->video_widget, TRUE);
	gtk_event_box_set_above_child (GTK_EVENT_BOX(d->video_widget), TRUE);
	gtk_widget_set_vexpand (d->video_widget, TRUE);
	g_signal_connect (d->video_widget, "draw",
					  G_CALLBACK (video_widget_draw_cb), d);


	gtk_container_add(GTK_CONTAINER (d->window_widget), d->video_widget);
	gtk_widget_show (d->video_widget);
	gtk_widget_show_all (d->window_widget);
}

static void
print_keyboard_help (void)
{
	g_print ("\n\nInteractive mode - keyboard controls:\n\n");
	g_print ("\tp:   Pause/Play\n");
	g_print ("\tq:   quit\n");
	g_print ("\n");
}

static gulong io_watch_id;
static void
keyboard_cb (const gchar key_input, gpointer user_data)
{
	DemoApp *d = user_data;
	gchar key = '\0';

	/* only want to switch/case on single char, not first char of string */
	if (key_input != '\0')
		key = g_ascii_tolower (key_input);
	switch (key) {
	case 'h':
		print_keyboard_help ();
	break;
	case 'p':
	{
		GstState actual_state;
		gst_element_get_state(d->pipeline, &actual_state, NULL, -1);
		if (actual_state == GST_STATE_PAUSED)
			gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
		else
			gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
	}
	break;
	case 'q':
		gst_element_set_state (d->pipeline, GST_STATE_NULL);
		local_kb_set_key_handler (NULL);
		g_main_loop_quit(loop);
		exit(1); //force to quit application
		break;
	}
}

static gboolean
io_callback (GIOChannel * io, GIOCondition condition, gpointer data)
{
	gchar in;
	GError *error = NULL;

	switch (g_io_channel_read_chars (io, &in, 1, NULL, &error)) {

	case G_IO_STATUS_NORMAL:
		keyboard_cb(in, data);
		return TRUE;
	case G_IO_STATUS_ERROR:
		g_printerr ("IO error: %s\n", error->message);
		g_error_free (error);
		return FALSE;
	case G_IO_STATUS_EOF:
		g_warning ("No input data available");
		return TRUE;
	case G_IO_STATUS_AGAIN:
		return TRUE;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return FALSE;
}
gboolean
local_kb_set_key_handler(gpointer user_data)
{
	GIOChannel *io;
	if (io_watch_id > 0) {
		g_source_remove (io_watch_id);
		io_watch_id = 0;
	}
	io = g_io_channel_unix_new (STDIN_FILENO);
	io_watch_id = g_io_add_watch (io, G_IO_IN, io_callback, user_data);
	g_io_channel_unref (io);
	return TRUE;
}
int
main (int argc, char **argv)
{
	DemoApp *d;
	GOptionContext *context;
	GstBus *bus;
	GError *error = NULL;
	guint bus_watch_id;

	gtk_init (&argc, &argv);
	gst_init (&argc, &argv);

	context = g_option_context_new ("- waylandsink gtk demo");
	g_option_context_add_main_entries (context, entries, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("option parsing failed: %s\n", error->message);
		return 1;
	}

	d = g_slice_new0 (DemoApp);
	build_window (d);

	if (argc > 1) {
		d->argv = argv;
		d->current_uri = 1;
		d->to_start = TRUE;

		if (!nofullscreen)
			d->pipeline = gst_parse_launch ("playbin video-sink='waylandsink fullscreen=true'", NULL);
		else
			d->pipeline = gst_parse_launch ("playbin video-sink='waylandsink'", NULL);

		g_object_set (d->pipeline, "uri", argv[d->current_uri], NULL);

		/* enable looping */
		g_signal_connect (d->pipeline, "about-to-finish",
						  G_CALLBACK (on_about_to_finish), d);
	} else {
		if (graph != NULL) {
			d->to_start = TRUE;
			if (strstr(graph, "waylandsink") != NULL) {
				d->pipeline = gst_parse_launch (graph, NULL);
			} else {
				g_print("ERROR: grap does not contains waylandsink !!!\n");
				g_free(graph);
				return 1;
			}
		} else if (shader_file != NULL) {
			gchar *shader_graph;
			gchar fragment_content[8096];
			FILE *fp;
			size_t nread, data_read, len=0;
			char *line;
			GstElement *customshader;

			d->to_start = TRUE;
			shader_graph = g_strdup_printf("v4l2src ! video/x-raw, format=YUY2, width=320, height=240, framerate=(fraction)15/1 ! videorate  ! video/x-raw,framerate=(fraction)5/1  !  queue ! videoconvert ! video/x-raw,format=RGBA ! queue ! glupload ! queue ! glshader name=customshader ! queue ! gldownload ! queue ! videoconvert ! queue ! waylandsink sync=false fullscreen=true");
			d->pipeline = gst_parse_launch (shader_graph, NULL);

			customshader = gst_bin_get_by_name(GST_BIN(d->pipeline), "customshader");
			g_assert(customshader);

			fp = fopen(shader_file, "r");
			if (fp == NULL) {
				g_print("ERROR: file cannot be openned, please specify absolute path!!\n");
				g_free(shader_file);
				return 1;
			} else {
				data_read = 0;
				bzero(fragment_content, sizeof fragment_content);
				while ((nread = getline(&line, &len, fp) ) != -1) {
					snprintf(fragment_content + data_read, 8096 - data_read, "%s", line);
					data_read += nread;
				}
				free(line);
				fclose(fp);
				//g_print("content: \n%s\n", fragment_content);
			}
			if (customshader) {
				//g_print("set fragment\n");
				g_object_set(customshader, "fragment", fragment_content, NULL);
			}

		} else {
			d->pipeline = gst_parse_launch ("videotestsrc pattern=18 "
					"background-color=0x000062FF ! waylandsink fullscreen=true", NULL);
		}
	}

	bus = gst_pipeline_get_bus (GST_PIPELINE (d->pipeline));
	bus_watch_id = gst_bus_add_watch (bus, gstreamer_bus_callback, d);
	gst_bus_set_sync_handler (bus, bus_sync_handler, d, NULL);
	gst_object_unref (bus);

	if (nofullscreen) {
		GstState state;
		GstElement *sink;

		gint i;
		struct _gst_caps_video_size video_size;
		video_size.width = video_size.height = 0;

		gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
		gst_element_seek_simple (d->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH, GST_SECOND * 1);
		gst_element_get_state (d->pipeline, &state, NULL, GST_SECOND * 5);

		switch (state){
		case GST_STATE_VOID_PENDING:
			g_print("state: GST_STATE_VOID_PENDING\n");break;
		case GST_STATE_NULL:
			g_print("state: GST_STATE_NULL\n");break;
		case GST_STATE_READY:
			g_print("state: GST_STATE_READY\n");
			break;
		case GST_STATE_PAUSED:
			g_print("state: GST_STATE_PAUSED\n");
			break;
		case GST_STATE_PLAYING:
			g_print("state: GST_STATE_PLAYING\n");
			break;
		default:
			break;
		}

		gst_element_seek_simple (d->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH, 0);
		gst_element_set_state (d->pipeline, GST_STATE_NULL);
		gst_element_set_state (d->pipeline, GST_STATE_PAUSED);
	}

	if (local_kb_set_key_handler (d)) {
		g_print ("Press 'h' to see a list of keyboard shortcuts.\n");
	} else {
		g_print ("Interactive keyboard handling in terminal not available.\n");
	}

	gst_element_set_state (d->pipeline, GST_STATE_PLAYING);
	{
		GstState actual_state;

		while (TRUE) {
			gst_element_get_state(d->pipeline, &actual_state, NULL, 2);
			switch (actual_state){
			case GST_STATE_VOID_PENDING:
				g_print("main state: GST_STATE_VOID_PENDING\n");
				break;
			case GST_STATE_NULL:
				g_print("main state: GST_STATE_NULL\n");
				break;
			case GST_STATE_READY:
				g_print("main state: GST_STATE_READY\n");
				break;
			case GST_STATE_PAUSED:
				g_print("main state: GST_STATE_PAUSED\n");
				break;
			case GST_STATE_PLAYING:
				g_print("main state: GST_STATE_PLAYING\n");
				break;
			default:
				break;
			}
			if (actual_state == GST_STATE_PLAYING)
				break;

			sleep(1);
		}
	}
	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);

	local_kb_set_key_handler (NULL);

	gst_element_set_state (d->pipeline, GST_STATE_NULL);
	gst_object_unref (d->pipeline);
	g_object_unref (d->window_widget);
	g_source_remove (bus_watch_id);
	g_slice_free (DemoApp, d);
	g_main_loop_unref (loop);

	g_free(graph);

	return 0;
}
