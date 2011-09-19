/*
 *  gstvaapidisplay_wayland_priv.h - Internal VA/Wayland interface
 *
 *  Copyright: Intel Corporation
 *  Copyright: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef GST_VAAPI_DISPLAY_WAYLAND_PRIV_H
#define GST_VAAPI_DISPLAY_WAYLAND_PRIV_H

#include <gst/vaapi/gstvaapiutils_wayland.h>
#include <gst/vaapi/gstvaapidisplay_wayland.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <va/va_wayland.h>

G_BEGIN_DECLS

#define GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(obj)                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DISPLAY_WAYLAND,	\
                                 GstVaapiDisplayWaylandPrivate))

#define GST_VAAPI_DISPLAY_WAYLAND_CAST(display) ((GstVaapiDisplayWayland *)(display))

/**
 * GST_VAAPI_DISPLAY_WDISPLAY:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the underlying Wayland #Display of @display
 */
#undef  GST_VAAPI_DISPLAY_WDISPLAY
#define GST_VAAPI_DISPLAY_WDISPLAY(display) \
    GST_VAAPI_DISPLAY_WAYLAND_CAST(display)->priv->wayland_display

struct WDisplay {
        struct wl_display *display;
        struct wl_compositor *compositor;
        struct wl_shell *shell;
        uint32_t mask;
        int event_fd;
};

struct _GstVaapiDisplayWaylandPrivate {
    gchar      *display_name;
    struct wl_display  *wayland_display;
};

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_WAYLAND_PRIV_H */
