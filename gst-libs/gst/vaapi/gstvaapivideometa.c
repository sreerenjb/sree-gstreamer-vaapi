/*
 *  gstvaapivideometa.c - Gst VA video meta
 *
 *  Copyright (C) 2012 Intel Corporation
 *  Copyright (C) : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *  Contact : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstvaapivideometa.h"

static gboolean
gst_vaapi_video_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
    GstVaapiVideoMeta *dmeta, *smeta;
    guint i;
    
    smeta = (GstVaapiVideoMeta *) meta;
    
    if (GST_META_TRANSFORM_IS_COPY (type)) {
        GstMetaTransformCopy *copy = data;
        if (!copy->region) {
        /* only copy if the complete data is copied as well */
        dmeta =
            (GstVaapiVideoMeta *) gst_buffer_add_meta (dest, GST_VAAPI_VIDEO_META_INFO,
                NULL);
        GST_DEBUG ("copy vaapi video metadata");
        dmeta->display      = g_object_ref(G_OBJECT(smeta->display));
        dmeta->video_mem  = (GstVaapiVideoMemory *)gst_memory_ref((GstMemory *)(smeta->video_mem));
        dmeta->render_flags = smeta->render_flags;
      }
    } 

    return TRUE;
}

static void
gst_vaapi_video_meta_free (GstVaapiVideoMeta * meta, GstBuffer * buffer)
{
    if (meta->display) {
        g_object_unref(G_OBJECT(meta->display));
	meta->display = NULL;
    }
    if (meta->video_mem) {
	gst_memory_unref((GstMemory *)(meta->video_mem));
	meta->video_mem = NULL;
    }   
}

/* vaapi_video metadata */
GType
gst_vaapi_video_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] =
      { "memory", "size", "colorspace", "orientation", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVaapiVideoMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_vaapi_video_meta_get_info (void)
{
  static const GstMetaInfo *vaapi_video_meta_info = NULL;

  if (vaapi_video_meta_info == NULL) {
    vaapi_video_meta_info =
        gst_meta_register (GST_VAAPI_VIDEO_META_API_TYPE, "GstVaapiVideoMeta",
        sizeof (GstVaapiVideoMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_vaapi_video_meta_free,
        (GstMetaTransformFunction) gst_vaapi_video_meta_transform);
  }
  return vaapi_video_meta_info;
}

GstVaapiVideoMeta*
gst_buffer_add_vaapi_video_meta (GstBuffer *buffer, GstVaapiDisplay *display)
{
    GstVaapiVideoMeta *meta;

    meta = (GstVaapiVideoMeta *)gst_buffer_add_meta (buffer, GST_VAAPI_VIDEO_META_INFO, NULL);

    meta->render_flags = 0x00000000;

    if (display)
        meta->display = g_object_ref(G_OBJECT(display));

    meta->video_mem = (GstVaapiVideoMemory *) gst_buffer_get_memory (buffer, 0);
   
    return meta;
}

GstVaapiSurface * gst_vaapi_video_meta_get_surface (GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(meta != NULL, NULL);
    if (meta->video_mem)
	return meta->video_mem->surface;
    else
	return NULL;
}

GstVaapiImage   * gst_vaapi_video_meta_get_image (GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(meta != NULL, NULL);
    if (meta->video_mem)
	return meta->video_mem->image;
    else
	return NULL;
}
