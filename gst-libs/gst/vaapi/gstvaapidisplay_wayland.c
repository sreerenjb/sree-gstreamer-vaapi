/*
 *  gstvaapidisplay_wayland.c - VA/Wayland display abstraction
 *
 *  Copyright: Intel Corporation
 *  Copyright: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
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
 * SECTION:gstvaapidisplay_wayland
 * @short_description: VA/Wayland display abstraction
 */

#include "config.h"
#include "gstvaapi_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <gst/gst.h>

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDisplayWayland,
              gst_vaapi_display_wayland,
              GST_VAAPI_TYPE_DISPLAY);

enum {
    PROP_0,

    PROP_SYNCHRONOUS,
    PROP_DISPLAY_NAME,
    PROP_WAYLAND_DISPLAY,
};

static void
gst_vaapi_display_wayland_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_display_wayland_parent_class)->finalize(object);
}

static void
set_display_name(GstVaapiDisplayWayland *display, const gchar *display_name)
{
    GstVaapiDisplayWaylandPrivate * const priv = display->priv;

    g_free(priv->display_name);

    if (display_name)
        priv->display_name = g_strdup(display_name);
    else
        priv->display_name = NULL;
}


static void
gst_vaapi_display_wayland_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        set_display_name(display, g_value_get_string(value));
        break;
    case PROP_WAYLAND_DISPLAY:
        display->priv->wayland_display = g_value_get_pointer(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_wayland_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        g_value_set_string(value, display->priv->display_name);
        break;
    case PROP_WAYLAND_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_wayland_get_display(display));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_wayland_constructed(GObject *object)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(object);
    GObjectClass *parent_class;


    //display->priv->create_display = display->priv->wayland_display == NULL;
    /* Reset display-name if the user provided his own Wayland display */
    //if (!display->priv->create_display)
      //  set_display_name(display, XDisplayString(display->priv->wayland_display));

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_wayland_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
compositor_handle_visual(void *data,
                         struct wl_compositor *compositor,
                         uint32_t id, uint32_t token)
{
        struct WDisplay *d = data;

        switch (token) {
        case WL_COMPOSITOR_VISUAL_XRGB32:
                d->rgb_visual = wl_visual_create(d->display, id, 1);
                break;
        }
}

static const struct wl_compositor_listener compositor_listener = {
        compositor_handle_visual,
};

static void
display_handle_global(struct wl_display *display, uint32_t id,
                      const char *interface, uint32_t version, void *data)
{
        struct WDisplay *d = data;

        if (strcmp(interface, "wl_compositor") == 0) {
                d->compositor = wl_compositor_create(display, id, 1);
                wl_compositor_add_listener(d->compositor,
                                           &compositor_listener, d);
        } else if (strcmp(interface, "wl_shell") == 0) {
                d->shell = wl_shell_create(display, id, 1);
        }
}

static int
event_mask_update(uint32_t mask, void *data)
{
        struct WDisplay *d = data;

        d->mask = mask;

        return 0;
}

static gboolean
gst_vaapi_display_wayland_open_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    GST_DEBUG ("Creating wayland display");
    if (priv->wayland_display)
    {
	GST_DEBUG ("wayland display already set from app ,returns..");
	return TRUE;
    }	
    struct WDisplay *d;
    /* XXX: maintain a Wayland display cache */
    if (!priv->wayland_display) {
        d = calloc(1, sizeof *d);
        if (!d)
                return FALSE;
        memset(d, 0, sizeof *d);

        d->display = wl_display_connect(NULL);
        if (!d->display)
                return NULL;

        wl_display_set_user_data(d->display, d);
        wl_display_add_global_listener(d->display,
                                       display_handle_global, d);
        d->event_fd = wl_display_get_fd(d->display, event_mask_update, d);

#if 1
        wl_display_iterate(d->display, d->mask);
#else
        wl_display_iterate(d->display, WL_DISPLAY_READABLE);
        wl_display_flush(d->display);
#endif

        priv->wayland_display = d->display;
        if (!priv->wayland_display)
            return FALSE;
    }
    if (!priv->wayland_display)
        return FALSE;

    return TRUE;
}

static void
gst_vaapi_display_wayland_close_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    if (priv->wayland_display) {
        struct WDisplay *d = wl_display_get_user_data(priv->wayland_display);
        free(d);
        wl_display_destroy(priv->wayland_display);
        priv->wayland_display = NULL;
    }

    if (priv->display_name) {
        g_free(priv->display_name);
        priv->display_name = NULL;
    }
}

static void
gst_vaapi_display_wayland_sync(GstVaapiDisplay *display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    /*if (priv->wayland_display) {
        GST_VAAPI_DISPLAY_LOCK(display);
        GST_VAAPI_DISPLAY_UNLOCK(display);
    }*/
}

static void
gst_vaapi_display_wayland_flush(GstVaapiDisplay *display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    /*if (priv->wayland_display) {
        GST_VAAPI_DISPLAY_LOCK(display);
        GST_VAAPI_DISPLAY_UNLOCK(display);
    }*/
}

static VADisplay
gst_vaapi_display_wayland_get_va_display(GstVaapiDisplay *display)
{
    return vaGetDisplay(GST_VAAPI_DISPLAY_WDISPLAY(display));
}

static void
gst_vaapi_display_wayland_get_size(
    GstVaapiDisplay *display,
    guint           *pwidth,
    guint           *pheight
)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;
    GST_DEBUG ("get the wayland display size");
    /*Fixme: not yet implemented */ 
    if (!priv->wayland_display)
        return;

}
static void
gst_vaapi_display_wayland_get_size_mm(
    GstVaapiDisplay *display,
    guint           *pwidth,
    guint           *pheight
)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    if (!priv->wayland_display)
        return;
    /*Fixme: not yet implemented*/
}

static void
gst_vaapi_display_wayland_class_init(GstVaapiDisplayWaylandClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayWaylandPrivate));

    object_class->finalize      = gst_vaapi_display_wayland_finalize;
    object_class->set_property  = gst_vaapi_display_wayland_set_property;
    object_class->get_property  = gst_vaapi_display_wayland_get_property;
    object_class->constructed   = gst_vaapi_display_wayland_constructed;

    dpy_class->open_display     = gst_vaapi_display_wayland_open_display;
    dpy_class->close_display    = gst_vaapi_display_wayland_close_display;
    dpy_class->sync             = gst_vaapi_display_wayland_sync;
    dpy_class->flush            = gst_vaapi_display_wayland_flush;
    dpy_class->get_display      = gst_vaapi_display_wayland_get_va_display;
    dpy_class->get_size         = gst_vaapi_display_wayland_get_size;
    dpy_class->get_size_mm      = gst_vaapi_display_wayland_get_size_mm;

    /**
     * GstVaapiDisplayWayland:wayland-display:
     *
     * The Wayland #Display that was created by gst_vaapi_display_wayland_new()
     * or that was bound from gst_vaapi_display_wayland_new_with_display().
     */
    g_object_class_install_property
        (object_class,
         PROP_WAYLAND_DISPLAY,
         g_param_spec_pointer("wayland-display",
                              "Wayland display",
                              "Wayland display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
    /**
     * GstVaapiDisplayWayland:display-name:
     *
     * The Wayland display name.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_NAME,
         g_param_spec_string("display-name",
                             "Wayland display name",
                             "Wayland display name",
                             NULL,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_wayland_init(GstVaapiDisplayWayland *display)
{
    GstVaapiDisplayWaylandPrivate *priv = GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(display);

    display->priv        = priv;
    priv->wayland_display    = NULL;
    priv->display_name   = NULL;
}

/**
 * gst_vaapi_display_wayland_new:
 * @display_name: the Wayland display name
 *
 * Opens an Wayland #Display using @display_name and returns a newly
 * allocated #GstVaapiDisplay object. The Wayland display will be cloed
 * when the reference count of the object reaches zero.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_wayland_new(const gchar *display_name)
{
    GST_DEBUG ("Creating the new wayland display");
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_WAYLAND,
                        "display-name", display_name,
                        NULL);
}

/**
 * gst_vaapi_display_wayland_new_with_display:
 * @wayland_display: an Wayland #Display
 *
 * Creates a #GstVaapiDisplay based on the Wayland @wayland_display
 * display. The caller still owns the display and must call
 * wl_display_destroy  when all #GstVaapiDisplay references are
 * released. Doing so too early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_display(struct wl_display *wayland_display)
{
    g_return_val_if_fail(wayland_display, NULL);

    return g_object_new(GST_VAAPI_TYPE_DISPLAY_WAYLAND,
                        "wayland-display", wayland_display,
                        NULL);
}

/**
 * gst_vaapi_display_wayland_get_display:
 * @display: a #GstVaapiDisplayWayland
 *
 * Returns the underlying Wayland #Display that was created by
 * gst_vaapi_display_wayland_new() or that was bound from
 * gst_vaapi_display_wayland_new_with_display().
 *
 * Return value: the Wayland #Display attached to @display
 */
struct wl_display *
gst_vaapi_display_wayland_get_display(GstVaapiDisplayWayland *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_WAYLAND(display), NULL);
    
    return display->priv->wayland_display;
}
