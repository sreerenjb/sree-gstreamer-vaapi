/*
 *  gstvaapisurfacemeta.h - Gst VA surface meta
 *
 *  Copyright (C) 2012 Intel Corporation
 *  Copyright (C) : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef _GST_VAAPI_SURFACE_META_H_
#define _GST_VAAPI_SURFACE_META_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfacememory.h>

G_BEGIN_DECLS

typedef struct _GstVaapiSurfaceMeta GstVaapiSurfaceMeta;

GType gst_vaapi_surface_meta_api_get_type (void);
#define GST_VAAPI_SURFACE_META_API_TYPE  (gst_vaapi_surface_meta_api_get_type())
const GstMetaInfo * gst_vaapi_surface_meta_get_info (void);
#define GST_VAAPI_SURFACE_META_INFO  (gst_vaapi_surface_meta_get_info())

struct _GstVaapiSurfaceMeta {
   GstMeta meta;

   GstVaapiDisplay *display;
   GstVaapiSurface *surface;
   GstVaapiSurfaceMemory *surface_mem;

   gboolean interlaced;
   gboolean tff;
   guint render_flags;
};

#define gst_buffer_get_vaapi_surface_meta(b) ((GstVaapiSurfaceMeta*)gst_buffer_get_meta((b),GST_VAAPI_SURFACE_META_API_TYPE))
GstVaapiSurfaceMeta * gst_buffer_add_vaapi_surface_meta (GstBuffer * buffer, GstVaapiDisplay *display);

G_END_DECLS

#endif /* _GST_VAPPI_SURFACE_META_H_ */
