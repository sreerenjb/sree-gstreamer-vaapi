/*
 *  gstvaapisurfacepool.c - Gst VA surface pool
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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
 * SECTION:gstvaapisurfacepool
 * @short_description: VA surface pool
 */

#include "sysdeps.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapisurfaceproxy.h"
#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(
    GstVaapiSurfacePool,
    gst_vaapi_surface_pool,
    GST_VAAPI_TYPE_VIDEO_POOL);

#define GST_VAAPI_SURFACE_POOL_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SURFACE_POOL,	\
                                 GstVaapiSurfacePoolPrivate))

struct _GstVaapiSurfacePoolPrivate {
    GstAllocator *allocator;
    
    GstVaapiChromaType  chroma_type;
    guint               width;
    guint               height; 
};

static void
gst_vaapi_surface_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL(pool)->priv;
    GstStructure *structure;
    gint width, height;
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
    {
	GST_ERROR ("Failed to parse video info");
	return;
    }

    priv->chroma_type   = GST_VAAPI_CHROMA_TYPE_YUV420;
    priv->width         = info.width;
    priv->height        = info.height;
}

static const gchar **
gst_vaapi_surface_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
gst_vaapi_surface_pool_set_config (GstBufferPool * pool, GstStructure * config)
{

  GstVaapiSurfacePool *surface_pool = GST_VAAPI_SURFACE_POOL (pool);
  GstVaapiSurfacePoolPrivate *priv = surface_pool->priv;
  GstVideoInfo info;
  GstCaps *caps;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  /* keep track of the width and height and caps */
  priv->width = info.width;
  priv->height = info.height;

  return GST_BUFFER_POOL_CLASS (gst_vaapi_surface_pool_parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }

  no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }

wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

}
  
static GstFlowReturn
gst_vaapi_surface_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{

   GstBuffer *surface_buffer;
   GstVaapiDisplay *display;
   GstMemory *surface_mem;

   GstVaapiVideoPool *video_pool = GST_VAAPI_VIDEO_POOL (pool);
   GstVaapiSurfacePool *surface_pool = GST_VAAPI_SURFACE_POOL (pool);
   GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL(pool)->priv;

   display = gst_vaapi_video_pool_get_display (video_pool);

   surface_mem = gst_allocator_alloc (priv->allocator , priv->width*priv->height, 0);

   surface_buffer = gst_buffer_new ();
   gst_buffer_insert_memory (surface_buffer, -1, (GstMemory *)surface_mem);
   GST_BUFFER_OFFSET(surface_buffer) = 0;
   GST_BUFFER_TIMESTAMP(surface_buffer) = 0;

   *buffer = surface_buffer;
   return GST_FLOW_OK;
}

static void
gst_vaapi_surface_pool_finalize(GObject *object)
{
    GstVaapiSurfacePool *pool = GST_VAAPI_SURFACE_POOL (object);
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(pool);

    if (priv->allocator)
	gst_allocator_unref (priv->allocator);

    G_OBJECT_CLASS(gst_vaapi_surface_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_pool_class_init(GstVaapiSurfacePoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiVideoPoolClass * const pool_class = GST_VAAPI_VIDEO_POOL_CLASS(klass);
    GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

    g_type_class_add_private(klass, sizeof(GstVaapiSurfacePoolPrivate));

    object_class->finalize      = gst_vaapi_surface_pool_finalize;

    pool_class->set_caps        = gst_vaapi_surface_pool_set_caps;

    gstbufferpool_class->get_options  = gst_vaapi_surface_pool_get_options;
    gstbufferpool_class->set_config   = gst_vaapi_surface_pool_set_config;
    gstbufferpool_class->alloc_buffer = gst_vaapi_surface_pool_alloc;
}

static void
gst_vaapi_surface_pool_init(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->chroma_type   = 0;
    priv->width         = 0;
    priv->height        = 0;
    priv->allocator     = NULL;
}

/**
 * gst_vaapi_surface_pool_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiSurface with the
 * specified dimensions in @caps.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_surface_pool_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiVideoPool *pool;
    
    pool = g_object_new(GST_VAAPI_TYPE_SURFACE_POOL,
                        "display", display,
                        "caps",    caps,
                        NULL);
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(pool);

    priv->allocator = gst_vaapi_create_allocator (display, caps);

    return pool;
}
