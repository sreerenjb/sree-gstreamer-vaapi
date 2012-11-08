/*
 *  gstvaapivideometa.h - Gst VA video meta
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

#ifndef _GST_VAAPI_VIDEO_META_H_
#define _GST_VAAPI_VIDEO_META_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapivideomemory.h>

G_BEGIN_DECLS

typedef struct _GstVaapiVideoMeta GstVaapiVideoMeta;

GType gst_vaapi_video_meta_api_get_type (void);
#define GST_VAAPI_VIDEO_META_API_TYPE  (gst_vaapi_video_meta_api_get_type())
const GstMetaInfo * gst_vaapi_video_meta_get_info (void);
#define GST_VAAPI_VIDEO_META_INFO  (gst_vaapi_video_meta_get_info())

struct _GstVaapiVideoMeta {
   GstMeta meta;

   GstVaapiDisplay *display;
   GstVaapiVideoMemory *video_mem;

   gboolean interlaced;
   gboolean tff;
   guint render_flags;
};

#define gst_buffer_get_vaapi_video_meta(b) ((GstVaapiVideoMeta*)gst_buffer_get_meta((b),GST_VAAPI_VIDEO_META_API_TYPE))
GstVaapiVideoMeta * gst_buffer_add_vaapi_video_meta (GstBuffer * buffer, GstVaapiDisplay *display);

GstVaapiSurface * gst_vaapi_video_meta_get_surface (GstVaapiVideoMeta *meta);
GstVaapiImage   * gst_vaapi_video_meta_get_image (GstVaapiVideoMeta *meta);

G_END_DECLS

#endif /* _GST_VAPPI_VIDEO_META_H_ */
