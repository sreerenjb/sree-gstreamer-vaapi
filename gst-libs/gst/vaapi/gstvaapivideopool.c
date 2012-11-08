/*
 *  gstvaapivideopool.c - Gst VA video pool
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
 * SECTION:gstvaapivideopool
 * @short_description: VA surface pool
 */

#include "sysdeps.h"
#include "gstvaapivideopool.h"
#include "gstvaapisurfaceproxy.h"
#define DEBUG 1
#include "gstvaapidebug.h"

struct _GstVaapiVideoPoolPrivate {
    GstAllocator 	*allocator;
  
    GstVaapiImageFormat yuv_format; 
    GstVaapiChromaType  chroma_type;

    GstVideoInfo	info;
    guint		add_videometa          :1;
    guint		add_vaapi_video_meta   :1; 
};

G_DEFINE_TYPE(
    GstVaapiVideoPool,
    gst_vaapi_video_pool,
    GST_TYPE_BUFFER_POOL)

#define GST_VAAPI_VIDEO_POOL_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_POOL,	\
                                 GstVaapiVideoPoolPrivate))

static const gchar **
gst_vaapi_video_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META, NULL
  };

  return options;
}

static gboolean
ensure_vaapi_video_memory (GstBufferPool *pool, GstVideoInfo *info)
{
    GstVaapiVideoPool *video_pool = GST_VAAPI_VIDEO_POOL (pool);
    GstVaapiVideoPoolPrivate *priv  = video_pool->priv;
    
    GstVaapiDisplay *display = video_pool->display;
 
    if(!priv->allocator) 
       priv->allocator = gst_allocator_find(GST_VAAPI_VIDEO_ALLOCATOR_NAME);
}

static gboolean
gst_vaapi_video_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
    GstVaapiVideoPool *video_pool = GST_VAAPI_VIDEO_POOL (pool);
    GstVaapiVideoPoolPrivate *priv = video_pool->priv;
    GstVaapiDisplay *display;
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

    priv->chroma_type   = GST_VAAPI_CHROMA_TYPE_YUV420;
    priv->info 		= info;
    
    /*Fixme: move this to vaapisink:propose_allocation*/
    gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
    gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
    
    if (!priv->allocator)    
        priv->allocator = gst_allocator_find(GST_VAAPI_VIDEO_ALLOCATOR_NAME);

    gst_allocation_params_init(&params);
    gst_buffer_pool_config_set_allocator(config, priv->allocator, &params);

    /* enable metadata based on config of the pool */
    priv->add_videometa =
        gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    /* parse extra alignment info */
    priv->add_vaapi_video_meta = gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);

    return GST_BUFFER_POOL_CLASS (gst_vaapi_video_pool_parent_class)->set_config (pool, config);

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
gst_vaapi_video_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * buf_alloc_params)
{

   GstBuffer *video_buffer;
   GstVaapiDisplay *display;
   GstMemory *video_mem;
   GstAllocationParams params;
   GstVaapiVideoMeta *meta;
   GstVideoInfo *info;

   GstVaapiVideoPool *video_pool = GST_VAAPI_VIDEO_POOL (pool);
   GstVaapiVideoPoolPrivate *priv = video_pool->priv;

   info = &priv->info;

   display = video_pool->display;

   if (!(video_mem = gst_vaapi_video_memory_new(display,info))) {
	GST_ERROR("Failed to create the VA video Memory");
	return FALSE;
   }

   video_buffer = gst_buffer_new ();
   gst_buffer_insert_memory (video_buffer, -1, (GstMemory *)video_mem);
   GST_BUFFER_OFFSET(video_buffer) = 0;
   GST_BUFFER_TIMESTAMP(video_buffer) = 0;
  
   if(priv->add_vaapi_video_meta) {
       meta = gst_buffer_add_vaapi_video_meta (video_buffer, display);
       if (!meta) {
           gst_buffer_unref(video_buffer);
           goto no_buffer;
       }
   }
   
   if (priv->add_videometa) {
       GstVideoMeta *vmeta;

       GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
       /* these are just the defaults for now */
       vmeta = gst_buffer_add_video_meta (video_buffer, 0, GST_VIDEO_INFO_FORMAT (info),
           GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
       
       vmeta->map   = gst_vaapi_video_memory_map;
       vmeta->unmap = gst_vaapi_video_memory_unmap;
   }
 
   *buffer = video_buffer;
   return GST_FLOW_OK;

/* ERROR */
no_buffer:
   {
       GST_WARNING_OBJECT (pool, "can't create image");
       return GST_FLOW_ERROR;
   }
}

static void
gst_vaapi_video_pool_finalize(GObject *object)
{
    GstVaapiVideoPool *pool = GST_VAAPI_VIDEO_POOL (object);
    GstVaapiVideoPoolPrivate *priv = GST_VAAPI_VIDEO_POOL_GET_PRIVATE(pool);

    if (priv->allocator)
	gst_object_unref (GST_OBJECT(priv->allocator));

    if (pool->display)
	g_object_unref (pool->display);

    G_OBJECT_CLASS(gst_vaapi_video_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_video_pool_class_init(GstVaapiVideoPoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

    g_type_class_add_private(klass, sizeof(GstVaapiVideoPoolPrivate));

    object_class->finalize      = gst_vaapi_video_pool_finalize;

    gstbufferpool_class->get_options  = gst_vaapi_video_pool_get_options;
    gstbufferpool_class->set_config   = gst_vaapi_video_pool_set_config;
    gstbufferpool_class->alloc_buffer = gst_vaapi_video_pool_alloc;
}

static void
gst_vaapi_video_pool_init(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate *priv = GST_VAAPI_VIDEO_POOL_GET_PRIVATE(pool);

    pool->priv                   = priv;
    priv->chroma_type   	 = 0;
    priv->allocator     	 = NULL;
    priv->add_videometa 	 = FALSE;
    priv->add_vaapi_video_meta   = FALSE;
}

/**
 * gst_vaapi_video_pool_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiVideoPool 
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstBufferPool *
gst_vaapi_video_pool_new(GstVaapiDisplay *display)
{
    GstVaapiVideoPool *pool;
    GstVideoInfo info;
    
    pool = g_object_new(GST_VAAPI_TYPE_VIDEO_POOL,
			NULL);
    pool->display = g_object_ref(display);

    gst_vaapi_video_memory_allocator_setup();

    return GST_BUFFER_POOL_CAST(pool);
}
