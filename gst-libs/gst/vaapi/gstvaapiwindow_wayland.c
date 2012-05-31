/*
 *  gstvaapiwindow_wayland.c - VA/Wayland window abstraction
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapiwindow_wayland
 * @short_description: VA/Wayland window abstraction
 */

#include "config.h"
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiwindow_wayland.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_wayland.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE (GstVaapiWindowWayland, gst_vaapi_window_wayland,
    GST_VAAPI_TYPE_WINDOW);

static void
gst_vaapi_window_wayland_constructed (GObject * object)
{
  GstVaapiWindowWayland *const window = GST_VAAPI_WINDOW_WAYLAND (object);
  GObjectClass *parent_class;

  parent_class = G_OBJECT_CLASS (gst_vaapi_window_wayland_parent_class);
  if (parent_class->constructed)
    parent_class->constructed (object);
}

static gboolean
gst_vaapi_window_wayland_create (GstVaapiWindow * window, guint * width,
    guint * height)
{
  GstVaapiWindowWayland *wayland_window = GST_VAAPI_WINDOW_WAYLAND (window);
  struct wl_display *display = GST_VAAPI_OBJECT_WDISPLAY (window);
  struct WDisplay *d = wl_display_get_user_data (display);
  struct wl_surface *surface;
  struct wl_shell_surface *shell_surface;

  GST_DEBUG ("Creating wayland window");

  surface = wl_compositor_create_surface (d->compositor);

  shell_surface = wl_shell_get_shell_surface(d->shell, surface);
  wl_shell_surface_set_toplevel(shell_surface);
  wl_shell_surface_set_fullscreen (shell_surface,WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, NULL);

  wayland_window->surface = surface;
  wayland_window->shell_surface = shell_surface;
  wayland_window->redraw_pending = 0;

  return TRUE;
}

static void
gst_vaapi_window_wayland_destroy (GstVaapiWindow * window)
{
  GstVaapiWindowWayland *wayland_window = GST_VAAPI_WINDOW_WAYLAND (window);

  if (wayland_window->shell_surface)
  	wl_shell_surface_destroy(wayland_window->shell_surface);
  if (wayland_window->surface)
        wl_surface_destroy(wayland_window->surface);
}

static gboolean
gst_vaapi_window_wayland_resize (GstVaapiWindow * window, guint width,
    guint height)
{
  GST_DEBUG ("resizing the window, width = %d height= %d", width, height);
  GstVaapiWindowWayland *wayland_window = GST_VAAPI_WINDOW_WAYLAND (window);
  return TRUE;
}

static void
frame_redraw_callback(void *data, struct wl_callback *callback, uint32_t time)
{
  GstVaapiWindowWayland *wayland_window = data;
  wayland_window->redraw_pending = 0;
  wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_callback_listener = {
    frame_redraw_callback
};

static gboolean
gst_vaapi_window_wayland_render (GstVaapiWindow * window,
    GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  VASurfaceID surface_id;
  VAStatus status;
  struct wl_buffer *buffer;
  struct wl_callback *callback;

  struct wl_display *wldpy = GST_VAAPI_OBJECT_WDISPLAY (window);
  VADisplay display = gst_vaapi_display_get_display (GST_VAAPI_OBJECT_DISPLAY(window));
  GstVaapiWindowWayland *wayland_window = GST_VAAPI_WINDOW_WAYLAND (window);
  
  surface_id = GST_VAAPI_OBJECT_ID (surface);
  if (surface_id == VA_INVALID_ID)
    return FALSE;
  
  GST_VAAPI_OBJECT_LOCK_DISPLAY (window);

  /* Wait for the previous frame to complete redraw */
  if (wayland_window->redraw_pending) 
	wl_display_iterate(wldpy, WL_DISPLAY_READABLE);
	
  status = vaGetSurfaceBufferWl(
  	display,
        surface_id,
        &buffer
  );

  if (!vaapi_check_status (status, "vaPutSurface()"))
    return FALSE;

  wl_surface_attach(
  	wayland_window->surface,
        buffer,
        0, 0
  );

  wl_surface_damage(
        wayland_window->surface,
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height
  );

  wl_display_iterate(wldpy, WL_DISPLAY_WRITABLE);
  wayland_window->redraw_pending = 1;
  callback = wl_surface_frame (wayland_window->surface);
  wl_callback_add_listener(callback, &frame_callback_listener, wayland_window);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
  return TRUE;
}

static void
gst_vaapi_window_wayland_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vaapi_window_wayland_parent_class)->finalize (object);
}

static void
gst_vaapi_window_wayland_class_init (GstVaapiWindowWaylandClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiWindowClass *const window_class = GST_VAAPI_WINDOW_CLASS (klass);


  object_class->finalize = gst_vaapi_window_wayland_finalize;
  object_class->constructed = gst_vaapi_window_wayland_constructed;
  window_class->create = gst_vaapi_window_wayland_create;
  window_class->destroy = gst_vaapi_window_wayland_destroy;
  window_class->render = gst_vaapi_window_wayland_render;
  window_class->resize = gst_vaapi_window_wayland_resize;
}

static void
gst_vaapi_window_wayland_init (GstVaapiWindowWayland * window)
{
  window->surface = NULL;
}

/**
 * gst_vaapi_window_wayland_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels
 * @height: the requested windo height, in pixels
 *
 * Creates a window with the specified @width and @height. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_wayland_new (GstVaapiDisplay * display, guint width,
    guint height)
{
  GST_DEBUG ("new window, size %ux%u", width, height);

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY (display), NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  return g_object_new (GST_VAAPI_TYPE_WINDOW_WAYLAND,
      "display", display,
      "id", GST_VAAPI_ID (0), "width", width, "height", height, NULL);
}
