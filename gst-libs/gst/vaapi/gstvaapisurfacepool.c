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
    GstMemoryInfo info;
    GstMemory *memory;
    GstVaapiSurfaceMemory *surface_mem;

    GstVaapiChromaType  chroma_type;
    guint               width;
    guint               height;
  
    GstCaps		*caps;

    GstVaapiSurface *surface;
    GstVaapiSurfaceProxy *proxy;
    guint num_avail_slots;

};

guint
gst_vaapi_surface_pool_n_avail_slots (GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL(pool)->priv;
    return priv->num_avail_slots;
  
}

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

  //priv->info = info;
 
  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  /* keep track of the width and height and caps */
  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_copy (caps);

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

   GstVideoInfo *info;
   GstBuffer *surface_buffer;
   //GstVaapiSurfaceMeta *meta;
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

   priv->num_avail_slots +=1;
   *buffer = surface_buffer;
   return GST_FLOW_OK;
}

static void
gst_vaapi_surface_pool_finalize(GObject *object)
{
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
    priv->num_avail_slots = 0;
}

static gpointer
gst_vaapi_surface_mem_map (GstVaapiSurfaceMemory *mem, gsize maxsize, GstMapFlags flags)
{
  return mem->surface;
}

static gboolean
gst_vaapi_surface_mem_unmap (GstVaapiSurfaceMemory *mem)
{
  return TRUE;
}

static void
gst_vaapi_surface_mem_free (GstVaapiSurfaceMemory *mem)
{
   
    //Fixme
   if (mem->memory.parent)
    gst_memory_unref (mem->memory.parent);

  if (mem->notify)
    mem->notify (mem->user_data);

   g_slice_free1 (mem->slice_size, mem);

}

static GstVaapiSurfaceMemory*
gst_vaapi_surface_mem_share (GstVaapiSurfaceMemory *mem, gssize offset, gsize size)
{
  return (GstVaapiSurfaceMemory *)gst_mini_object_ref(GST_MINI_OBJECT_CAST(mem));
}

static void
_vaapi_surface_memory_allocator_notify (gpointer user_data)
{
  g_warning ("Vaapi Surface Memory Allocator was freed..!");
}

static void
default_mem_init (GstVaapiSurfaceMemory * mem, GstMemoryFlags flags,
    GstMemory * parent, gsize maxsize, gsize offset, gsize size, gsize align, gsize slice_size)
{
   mem->memory.flags = flags;
   mem->memory.refcount = 1;
   mem->memory.parent = parent ? gst_memory_ref (parent) : NULL;
   mem->memory.state = (flags & GST_MEMORY_FLAG_READONLY ? 0x1 : 0);
   mem->memory.maxsize = maxsize;
   mem->memory.align = align;
   mem->memory.offset = offset;
   mem->memory.size = size;
   mem->slice_size = slice_size;
}

static GstVaapiSurfaceMemory*
vaapi_surface_new_mem (gpointer user_data, GstMemoryFlags flags, gsize maxsize, gsize align, gsize offset, gsize size)
{
  gsize  slice_size;
  GstVaapiSurfaceMemory *surface_mem;
  GstVaapiDisplay *display;
  GstVaapiVideoPool   *video_pool;
  GstVaapiSurfacePool *surface_pool;
  video_pool   = (GstVaapiVideoPool *)user_data;
  surface_pool = (GstVaapiSurfacePool *) video_pool;
  GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(surface_pool);

  display = gst_vaapi_video_pool_get_display (video_pool);

  slice_size = sizeof (GstVaapiSurfaceMemory);

  surface_mem = g_slice_alloc (slice_size);
  if (surface_mem == NULL)
  {
        GST_ERROR ("Failed to allcoate GstVaapiSurfaceMemory....");
        return NULL;
  }
  
  default_mem_init (surface_mem, flags, NULL, maxsize,
      offset, size, align, slice_size);

  surface_mem->surface = gst_vaapi_surface_new(
            display,
            GST_VAAPI_CHROMA_TYPE_YUV420,
            priv->width, priv->height
        );
  return surface_mem;
 
}

static GstMemory*
gst_vaapi_surface_mem_alloc_alloc (GstAllocator *allocator, gsize size,  GstAllocationParams * params, gpointer user_data)
{
  GstMemory *mem;
  gsize maxsize;

  maxsize = size + params->prefix + params->padding;

  mem =  (GstMemory *)vaapi_surface_new_mem  (user_data, params->flags, maxsize, params->align, params->prefix, size);
  mem->allocator = allocator;
  mem->offset = 0;
  return mem;
}


static void
create_vaapi_allocator (GstVaapiVideoPool *pool)
{
    GstAllocator *allocator;
    static const GstMemoryInfo info;
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(pool);
  
    static const GstMemoryInfo surface_mem_info = {
      GST_ALLOCATOR_SURFACE_MEMORY,
      (GstAllocatorAllocFunction) gst_vaapi_surface_mem_alloc_alloc,
      (GstMemoryMapFunction) gst_vaapi_surface_mem_map,
      (GstMemoryUnmapFunction) gst_vaapi_surface_mem_unmap,
      (GstMemoryFreeFunction) gst_vaapi_surface_mem_free,
      (GstMemoryCopyFunction)  NULL,
      (GstMemoryShareFunction) gst_vaapi_surface_mem_share,
      (GstMemoryIsSpanFunction) NULL,
    };

    priv->info	    = info;

    allocator = gst_allocator_new (&surface_mem_info, pool, _vaapi_surface_memory_allocator_notify);
    gst_allocator_register ("GstVaapiSurfaceMemoryAllocator", allocator);
    
    priv->allocator = allocator;
 
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
    create_vaapi_allocator (pool);

    return pool;
}
