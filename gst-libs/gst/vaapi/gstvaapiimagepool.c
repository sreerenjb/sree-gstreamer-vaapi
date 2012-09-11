/*
 *  gstvaapiimagepool.c - Gst VA image pool
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
 * SECTION:gstvaapiimagepool
 * @short_description: VA image pool
 */

#include "sysdeps.h"
#include "gstvaapiimagepool.h"

#define DEBUG 1
#include "gstvaapidebug.h"

struct _GstVaapiImagePoolPrivate {
    GstAllocator	*allocator;
    GstVaapiImageFormat format;
    guint               width;
    guint               height;
};

static void gst_vaapi_image_meta_free (GstVaapiImageMeta * meta, GstBuffer * buffer);

/* vaapi_image metadata */
GType
gst_vaapi_image_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] =
      { "memory", "size", "colorspace", "orientation", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVaapiImageMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_vaapi_image_meta_get_info (void)
{
  static const GstMetaInfo *vaapi_image_meta_info = NULL;

  if (vaapi_image_meta_info == NULL) {
    vaapi_image_meta_info =
        gst_meta_register (GST_VAAPI_IMAGE_META_API_TYPE, "GstVaapiImageMeta",
        sizeof (GstVaapiImageMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_vaapi_image_meta_free,
        (GstMetaTransformFunction) NULL);
  }
  return vaapi_image_meta_info;
}

static GstVaapiImageMeta*
gst_buffer_add_vaapi_image_meta (GstBuffer *buffer, GstVaapiImagePool *pool)
{
    GstVaapiImageMeta *meta;
    GstVaapiImagePoolPrivate *priv;
    GstVaapiDisplay *display;
    gint width, height;

    priv = pool->priv;
    /*Fixme: implement stride, padding support*/
    width  = priv->width;
    height = priv->height;

    meta = (GstVaapiImageMeta *)gst_buffer_add_meta (buffer, GST_VAAPI_IMAGE_META_INFO, NULL);

    meta->width  = width;
    meta->height = height;
    meta->render_flags = 0x00000000;

    display = gst_vaapi_video_pool_get_display(GST_VAAPI_VIDEO_POOL(pool));
    if (display)
        meta->display = g_object_ref(G_OBJECT(display));

    return meta;
}

static void
gst_vaapi_image_meta_free (GstVaapiImageMeta * meta, GstBuffer * buffer)
{
    GstVaapiDisplay *display;

    display = meta->display;

    GST_DEBUG_OBJECT (display, "free meta on buffer %p", buffer);
    if (display)
        g_object_unref(G_OBJECT(display));
}

G_DEFINE_TYPE(
    GstVaapiImagePool,
    gst_vaapi_image_pool,
    GST_VAAPI_TYPE_VIDEO_POOL)

#define GST_VAAPI_IMAGE_POOL_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_IMAGE_POOL,	\
                                 GstVaapiImagePoolPrivate))


static void
gst_vaapi_image_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps)
{
    GstVaapiImagePoolPrivate * const priv = GST_VAAPI_IMAGE_POOL(pool)->priv;
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
    {
        GST_ERROR ("Failed to parse video info");
        return;
    }

    priv->format        = gst_vaapi_image_format_from_caps(caps);
    priv->width         = info.width;
    priv->height        = info.height;
}

static const gchar **
gst_vaapi_image_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
gst_vaapi_image_pool_set_config (GstBufferPool * pool, GstStructure * config)
{

  GstVaapiImagePool *image_pool = GST_VAAPI_IMAGE_POOL (pool);
  GstVaapiImagePoolPrivate *priv = image_pool->priv;
  GstAllocationParams params;
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

  gst_allocation_params_init(&params);
  gst_buffer_pool_config_set_allocator(config, priv->allocator, &params);

  return GST_BUFFER_POOL_CLASS (gst_vaapi_image_pool_parent_class)->set_config (pool, config);

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
gst_vaapi_image_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * buf_alloc_params)
{
   GstBuffer *image_buffer;
   GstVaapiDisplay *display;
   GstMemory *image_mem;
   GstAllocationParams params;
   GstVaapiImageMeta *meta;

   GstVaapiVideoPool *video_pool = GST_VAAPI_VIDEO_POOL (pool);
   GstVaapiImagePool *image_pool = GST_VAAPI_IMAGE_POOL (pool);
   GstVaapiImagePoolPrivate *priv = GST_VAAPI_IMAGE_POOL(pool)->priv;

   display = gst_vaapi_video_pool_get_display (video_pool);

   gst_allocation_params_init(&params);
   image_mem = gst_allocator_alloc (priv->allocator , priv->width*priv->height, &params);

   image_buffer = gst_buffer_new ();
   gst_buffer_insert_memory (image_buffer, -1, (GstMemory *)image_mem);
   GST_BUFFER_OFFSET(image_buffer) = 0;
   GST_BUFFER_TIMESTAMP(image_buffer) = 0;

   meta = gst_buffer_add_vaapi_image_meta (image_buffer, image_pool);
   if (!meta) {
       gst_buffer_unref(image_buffer);
       goto no_buffer;
   }

   *buffer = image_buffer;
   return GST_FLOW_OK;

/* ERROR */
no_buffer:
   {
       GST_WARNING_OBJECT (pool, "can't create image");
       return GST_FLOW_ERROR;
   }
}

static void
gst_vaapi_image_pool_finalize(GObject *object)
{
    GstVaapiImagePool *pool = GST_VAAPI_IMAGE_POOL (object);
    GstVaapiImagePoolPrivate *priv = GST_VAAPI_IMAGE_POOL_GET_PRIVATE(pool);

    if (priv->allocator)
        gst_object_unref (GST_OBJECT(priv->allocator));

    G_OBJECT_CLASS(gst_vaapi_image_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_image_pool_class_init(GstVaapiImagePoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiVideoPoolClass * const pool_class = GST_VAAPI_VIDEO_POOL_CLASS(klass);
    GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

    g_type_class_add_private(klass, sizeof(GstVaapiImagePoolPrivate));

    object_class->finalize      = gst_vaapi_image_pool_finalize;

    pool_class->set_caps        = gst_vaapi_image_pool_set_caps;

    gstbufferpool_class->get_options  = gst_vaapi_image_pool_get_options;
    gstbufferpool_class->set_config   = gst_vaapi_image_pool_set_config;
    gstbufferpool_class->alloc_buffer = gst_vaapi_image_pool_alloc;

}

static void
gst_vaapi_image_pool_init(GstVaapiImagePool *pool)
{
    GstVaapiImagePoolPrivate *priv = GST_VAAPI_IMAGE_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->format        = 0;
    priv->width         = 0;
    priv->height        = 0;
    priv->allocator     = NULL;
}

/**
 * gst_vaapi_image_pool_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiImage with the
 * specified dimensions in @caps.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_image_pool_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiVideoPool *pool;

    pool = g_object_new(GST_VAAPI_TYPE_IMAGE_POOL,
                        "display", display,
                        "caps",    caps,
                        NULL);

    GstVaapiImagePoolPrivate *priv = GST_VAAPI_IMAGE_POOL_GET_PRIVATE(pool);

    if (!gst_vaapi_image_memory_init(display,caps)) {
        GST_ERROR("Failed to initialize the image_memory and image_meory allocator");
        return NULL;
    }

    priv->allocator = gst_allocator_find("GstVaapiImageAllocator");

    return pool;

}
