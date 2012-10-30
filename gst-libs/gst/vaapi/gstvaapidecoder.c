/*
 *  gstvaapidecoder.c - VA decoder abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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
 * SECTION:gstvaapidecoder
 * @short_description: VA decoder abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapidecoder.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoder, gst_vaapi_decoder, G_TYPE_OBJECT)

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CAPS,

    N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

static inline void
push_surface(GstVaapiDecoder *decoder, GstVaapiSurfaceProxy *proxy)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstClockTime duration;

    GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(gst_vaapi_surface_proxy_get_surface_id(proxy)));

    if (priv->fps_n && priv->fps_d) {
        /* Actual field duration is computed in vaapipostproc */
        duration = gst_util_uint64_scale(GST_SECOND, priv->fps_d, priv->fps_n);
        gst_vaapi_surface_proxy_set_duration(proxy, duration);
    }
    g_queue_push_tail(priv->surfaces, proxy);
}

static inline GstVaapiSurfaceProxy *
pop_surface(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    return g_queue_pop_head(priv->surfaces);
}

static inline void
set_codec_data(GstVaapiDecoder *decoder, GstBuffer *codec_data)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->codec_data) {
        gst_buffer_unref(priv->codec_data);
        priv->codec_data = NULL;
    }

    if (codec_data)
        priv->codec_data = gst_buffer_ref(codec_data);
}

static void
set_caps(GstVaapiDecoder *decoder, GstCaps *caps)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVaapiProfile profile;
    const GValue *v_codec_data;
    gint v1, v2;
    gboolean b;
    const gchar *interlaced_mode;

    profile = gst_vaapi_profile_from_caps(caps);
    if (!profile)
        return;

    priv->caps = gst_caps_copy(caps);

    priv->codec = gst_vaapi_profile_get_codec(profile);
    if (!priv->codec)
        return;

    if (gst_structure_get_int(structure, "width", &v1))
        priv->width = v1;
    if (gst_structure_get_int(structure, "height", &v2))
        priv->height = v2;

    if (gst_structure_get_fraction(structure, "framerate", &v1, &v2)) {
        priv->fps_n = v1;
        priv->fps_d = v2;
    }

    if (gst_structure_get_fraction(structure, "pixel-aspect-ratio", &v1, &v2)) {
        priv->par_n = v1;
        priv->par_d = v2;
    }

    interlaced_mode = gst_structure_get_string(structure, "interlace-mode");
    if (interlaced_mode && g_strcmp0 (interlaced_mode, "progressive"))
        priv->is_interlaced = TRUE;
    
    v_codec_data = gst_structure_get_value(structure, "codec_data");
    if (v_codec_data)
        set_codec_data(decoder, gst_value_get_buffer(v_codec_data));
}

static void
clear_queue(GQueue *q, GDestroyNotify destroy)
{
    while (!g_queue_is_empty(q))
        destroy(g_queue_pop_head(q));
}

static void
gst_vaapi_decoder_finalize(GObject *object)
{
    GstVaapiDecoder * const        decoder = GST_VAAPI_DECODER(object);
    GstVaapiDecoderPrivate * const priv    = decoder->priv;

    set_codec_data(decoder, NULL);

    if (priv->caps) {
        gst_caps_unref(priv->caps);
        priv->caps = NULL;
    }

    if (priv->context) {
        g_object_unref(priv->context);
        priv->context = NULL;
        priv->va_context = VA_INVALID_ID;
    }
 
    if (priv->surfaces) {
        clear_queue(priv->surfaces, (GDestroyNotify)g_object_unref);
        g_queue_free(priv->surfaces);
        priv->surfaces = NULL;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
        priv->va_display = NULL;
    }

    G_OBJECT_CLASS(gst_vaapi_decoder_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDecoder * const        decoder = GST_VAAPI_DECODER(object);
    GstVaapiDecoderPrivate * const priv    = decoder->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        priv->display = g_object_ref(g_value_get_object(value));
        if (priv->display)
            priv->va_display = gst_vaapi_display_get_display(priv->display);
        else
            priv->va_display = NULL;
        break;
    case PROP_CAPS:
        set_caps(decoder, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_decoder_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDecoderPrivate * const priv = GST_VAAPI_DECODER(object)->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, priv->display);
        break;
    case PROP_CAPS:
        gst_value_set_caps(value, priv->caps);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_decoder_class_init(GstVaapiDecoderClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderPrivate));

    object_class->finalize     = gst_vaapi_decoder_finalize;
    object_class->set_property = gst_vaapi_decoder_set_property;
    object_class->get_property = gst_vaapi_decoder_get_property;

    /**
     * GstVaapiDecoder:display:
     *
     * The #GstVaapiDisplay this decoder is bound to.
     */
    g_properties[PROP_DISPLAY] =
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this decoder is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

    g_properties[PROP_CAPS] =
         g_param_spec_pointer("caps",
                              "Decoder caps",
                              "The decoder caps",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties(object_class, N_PROPERTIES, g_properties);
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);

    decoder->priv               = priv;
    priv->display               = NULL;
    priv->va_display            = NULL;
    priv->context               = NULL;
    priv->va_context            = VA_INVALID_ID;
    priv->caps                  = NULL;
    priv->codec                 = 0;
    priv->codec_data            = NULL;
    priv->pool			= NULL;
    priv->width                 = 0;
    priv->height                = 0;
    priv->fps_n                 = 0;
    priv->fps_d                 = 0;
    priv->par_n                 = 0;
    priv->par_d                 = 0;
    priv->surfaces              = g_queue_new();
    priv->is_interlaced         = FALSE;
}

/**
 * gst_vaapi_decoder_get_codec:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder codec type.
 *
 * Return value: the #GstVaapiCodec type for @decoder
 */
GstVaapiCodec
gst_vaapi_decoder_get_codec(GstVaapiDecoder *decoder)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), (GstVaapiCodec)0);

    return decoder->priv->codec;
}

/**
 * gst_vaapi_decoder_get_caps:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder caps. The deocder owns the returned caps, so
 * use gst_caps_ref() whenever necessary.
 *
 * Return value: the @decoder caps
 */
GstCaps *
gst_vaapi_decoder_get_caps(GstVaapiDecoder *decoder)
{
    return decoder->priv->caps;
}

/**
 * gst_vaapi_decoder_start:
 * @decoder: a #GstVaapiDecoder
 * 
 * Called when the element start processing
 */
gboolean
gst_vaapi_decoder_start(GstVaapiDecoder *decoder)
{
     GstVaapiDecoderClass *dec_class;
     if (decoder) 
	dec_class = GST_VAAPI_DECODER_GET_CLASS(decoder);
     if (decoder && dec_class->start)
         return  dec_class->start(decoder);
     else 
	GST_DEBUG("start() virtual method is not implemented or the decoder is not yet started");

    return TRUE;
}
   
/**
 * gst_vaapi_decoder_parse:
 * @decoder: a #GstVaapiDecoder
 * @adapter: a #GstAdapter which has encoded data to parse (owned by GstVideoDecoder)
 * @toadd  : size of data to be added to GstVideoCodecFrame
 *
 * Parse the data from adapter.
 * If the function returns GST_VAAPI_DECODER_STATUS_SUCCESS with toadd==0,
 * then it is the indication for vaapidecode to call finish_frame()
 *
 * Return value: status of parsing as a #GstVaapiDecoderStatus
 */
GstVaapiDecoderStatus
gst_vaapi_decoder_parse(
    GstVaapiDecoder *decoder, 
    GstAdapter *adapter, 
    guint *toadd, 
    gboolean *have_frame)
{
    GstVaapiDecoderStatus status;
    guint tries = 0;
/*Fixme*/
    for (tries=0; tries<100; tries++) {
        status = gst_vaapi_decoder_check_status(decoder);
        
        if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE) 
	{
            GST_DEBUG ("Waiting to get the free surface, 10ms in each try");
            g_usleep(10000);
	}
	else
	    break;
	
    }

    if (tries==100)
	goto error_decode_timeout;
    
    status = GST_VAAPI_DECODER_GET_CLASS(decoder)->parse(decoder, adapter, toadd, have_frame);
    GST_DEBUG("decode frame (status = %d)", status);
 
    return status; 

error_decode_timeout:
    {
        GST_DEBUG("decode timeout. Decoder required a VA surface but none "
                  "got available within one second");
        return GST_FLOW_EOS;
    }
}

/**
 * gst_vaapi_decoder_decide_allocation
 * @decoder: a #GstVaapiDecoder
 * @Query: query contains the result of downstream allocation query 
 */
gboolean
gst_vaapi_decoder_decide_allocation(
    GstVaapiDecoder *decoder,
    GstQuery *query
)
{  
    GstBufferPool *pool = NULL;
    GstAllocator  *allocator = NULL;
    GstCaps *caps;
    GstStructure *config;
    GstVideoInfo info;
    guint size = 0, min_buffers = 0, max_buffers = 0;
    gboolean  result;
    guint num_pools;
    guint i;
    static GstAllocationParams params = { 0, 15, 0, 0, };
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    gst_query_parse_allocation (query, &caps, NULL);
    gst_video_info_init (&info);
    gst_video_info_from_caps (&info, caps);

    pool = priv->pool;

    if (pool != NULL) {
        GstCaps *pcaps;
	GstStructure *pconfig;
	guint psize;
        pconfig = gst_buffer_pool_get_config (pool);
        gst_buffer_pool_config_get_params (pconfig, &pcaps, &psize, NULL, NULL);

        if (!gst_caps_is_equal (caps, pcaps)) {
      	    GST_DEBUG_OBJECT (pool, "pool has different caps");
	    /* different caps, we can't use this pool */
      	    gst_object_unref (pool);
      	    pool = NULL;
        }
        gst_structure_free (pconfig);
    }

    if (pool == NULL) {
        /* Fixme: Add a namespace for GST_VAAPI_SURFACE_META, so that any element other than 
         * vaapisink should not be allowed to add this option */
        num_pools = gst_query_get_n_allocation_pools (query);
        GST_DEBUG("Query has %d pools", num_pools);
    
    	for (i=0; i<num_pools; i++){
	    gst_query_parse_nth_allocation_pool (query, i, &pool, &size, &min_buffers, &max_buffers);
            config = gst_buffer_pool_get_config(pool);
	    
	    /* query might have many pools.If vaapisink is the renderer,
	    * then query should have a pool with VAAPI_SURFACE_META option */
	    if (gst_buffer_pool_config_has_option (config, GST_BUFFER_POOL_OPTION_VAAPI_SURFACE_META)) {
  	        GST_DEBUG_OBJECT(decoder, "vaapisink is the renderer, use the pool supplied by vaapisink");
		gst_structure_free (config);
		/* decoder is keeping an extra ref to the pool*/
		g_object_ref(pool);
	        break;
	    } else {
		gst_structure_free(config);
	        pool = NULL;
	    }
        }
    
        if(!pool) {	
            pool = gst_vaapi_surface_pool_new(
                priv->display,
                caps
            );
        
	if (!pool)
	    goto failed_pool;
        }	
	config = gst_buffer_pool_get_config(pool);
        allocator = gst_allocator_find(GST_VAAPI_SURFACE_ALLOCATOR_NAME);
	gst_buffer_pool_config_set_params (config, caps, info.size, min_buffers, max_buffers);
    	gst_buffer_pool_config_set_allocator (config, allocator, &params);
	if (!gst_buffer_pool_set_config (pool, config))
            goto failed_config;
    }

    gst_query_add_allocation_pool (query, pool, info.size, min_buffers, max_buffers);

    priv->pool = pool;

    result = GST_VAAPI_DECODER_GET_CLASS(decoder)->decide_allocation(decoder, pool);

    GST_DEBUG("decide_allocation status: %d ",result);
    return result;

failed_caps:
    {
        GST_ERROR_OBJECT(pool, "failed to create the caps for surface pool");
	return FALSE;
    }  
failed_pool:
    {
	GST_ERROR_OBJECT(pool, "failed to create the surface pool from decoder");
	return FALSE;
    }
failed_config:
    {
	GST_ERROR_OBJECT(pool, "failed to set the config in pool");
	return FALSE;
    }
}

/**
 * gst_vaapi_decoder_get_surface:
 * @decoder: a #GstVaapiDecoder
 * @frame: a #GstVideoCodecFrame to decode
 * @pstatus: return location for the decoder status, or %NULL
 *
 * Flushes encoded buffers to the decoder and returns a decoded
 * surface, if any.
 *
 * Return value: a #GstVaapiSurfaceProxy holding the decoded surface,
 *   or %NULL if none is available (e.g. an error). Caller owns the
 *   returned object. g_object_unref() after usage.
 */
GstVaapiSurfaceProxy *
gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GstVideoCodecFrame	  *frame,
    GstVaapiDecoderStatus *pstatus
) 
{
    GstVaapiSurfaceProxy *proxy = NULL;
    GstVaapiDecoderStatus status;

    if (pstatus)
        *pstatus = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    if (!frame) {
    	proxy = pop_surface(decoder);
	GST_INFO_OBJECT(decoder, "pop out the pending surfaces, proxy=%p",proxy);
	goto beach;
    } 

    status = GST_VAAPI_DECODER_GET_CLASS(decoder)->decode(decoder, frame);
    GST_DEBUG("decode frame (status = %d)", status);
    
    proxy = pop_surface(decoder);

beach:
    if (proxy)
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    if (pstatus)
        *pstatus = status;

    return proxy;
}
/** gst_vaapi_decoder_reset
 * @decoder: a $GstVaapiDecoder
 *
 * Allows to perform post-seek semantics reset (eg: clear the buffers queued)
 */
gboolean
gst_vaapi_decoder_reset(GstVaapiDecoder * decoder)
{
    gboolean res;
    
    res = GST_VAAPI_DECODER_GET_CLASS(decoder)->reset(decoder);
   
    return res; 
}

/**
 * gst_vaapi_decoder_start:
 * @decoder: a #GstVaapiDecoder
 * 
 * Called when the element start processing
 */
gboolean
gst_vaapi_decoder_stop(GstVaapiDecoder *decoder)
{
     GstVaapiDecoderClass *dec_class;

     if(decoder)
	 dec_class = GST_VAAPI_DECODER_GET_CLASS(decoder);
     if (decoder && dec_class->stop)
         return  dec_class->stop(decoder);
     else 
	GST_DEBUG("stop() virtual method is not implemented");
     
     return TRUE;
}

void
gst_vaapi_decoder_set_picture_size(
    GstVaapiDecoder    *decoder,
    guint               width,
    guint               height
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    gboolean size_changed = FALSE;

    if (priv->width != width) {
        GST_DEBUG("picture width changed to %d", width);
        priv->width = width;
        gst_caps_set_simple(priv->caps, "width", G_TYPE_INT, width, NULL);
        size_changed = TRUE;
    }

    if (priv->height != height) {
        GST_DEBUG("picture height changed to %d", height);
        priv->height = height;
        gst_caps_set_simple(priv->caps, "height", G_TYPE_INT, height, NULL);
        size_changed = TRUE;
    }

    if (size_changed)
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
}

void
gst_vaapi_decoder_set_framerate(
    GstVaapiDecoder    *decoder,
    guint               fps_n,
    guint               fps_d
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!fps_n || !fps_d)
        return;

    if (priv->fps_n != fps_n || priv->fps_d != fps_d) {
        GST_DEBUG("framerate changed to %u/%u", fps_n, fps_d);
        priv->fps_n = fps_n;
        priv->fps_d = fps_d;
        gst_caps_set_simple(
            priv->caps,
            "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
            NULL
        );
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
    }
}

void
gst_vaapi_decoder_set_pixel_aspect_ratio(
    GstVaapiDecoder    *decoder,
    guint               par_n,
    guint               par_d
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!par_n || !par_d)
        return;

    if (priv->par_n != par_n || priv->par_d != par_d) {
        GST_DEBUG("pixel-aspect-ratio changed to %u/%u", par_n, par_d);
        priv->par_n = par_n;
        priv->par_d = par_d;
        gst_caps_set_simple(
            priv->caps,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
            NULL
        );
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
    }
}

void
gst_vaapi_decoder_set_interlaced(GstVaapiDecoder *decoder, gboolean interlaced)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->is_interlaced != interlaced) {
        GST_DEBUG("interlaced changed to %s", interlaced ? "true" : "false");
        priv->is_interlaced = interlaced;
	
        gst_caps_set_simple(
            priv->caps,
            "interlace-mode", G_TYPE_STRING, (interlaced ? "mixed" : "progressive"),
            NULL
        );
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
    }
}

void
gst_vaapi_decoder_emit_caps_change(
    GstVaapiDecoder   *decoder,
    guint width,
    guint height
)
{
    gst_vaapi_decoder_set_picture_size(decoder, width, height);
    g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
}

gboolean
gst_vaapi_decoder_ensure_context(
    GstVaapiDecoder     *decoder,
    GstVaapiContextInfo *cip
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    gst_vaapi_decoder_set_picture_size(decoder, cip->width, cip->height);
		
    if (priv->context) {
        if (!gst_vaapi_context_reset_full(priv->context, cip))
            return FALSE;
    }
    else {
	priv->context = gst_vaapi_context_new_full(priv->display, cip);
        if (!priv->context)
            return FALSE;
    }
    priv->va_context = gst_vaapi_context_get_id(priv->context);
    return TRUE;
}


void
gst_vaapi_decoder_push_surface_proxy(
    GstVaapiDecoder      *decoder,
    GstVaapiSurfaceProxy *proxy
)
{
    push_surface(decoder, proxy);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_check_status(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiSurfacePool *pool;
    GstBuffer *buffer;
    GstBufferPoolAcquireParams params = {0, };
    GstFlowReturn ret;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;
/*Fixme: This might be  bit expensive operation if checking for every frame..? */
#if 0    
    if (priv->context) {
       pool = gst_vaapi_context_get_surface_pool(priv->context);

       if (pool && !GST_BUFFER_POOL_IS_FLUSHING((GST_BUFFER_POOL(pool)))) {
           params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
           ret = gst_buffer_pool_acquire_buffer((GstBufferPool *)pool, &buffer,&params);
           
           if (ret != GST_FLOW_OK) 
               status = GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE;
           else if (ret == GST_FLOW_OK) {
	       status = GST_VAAPI_DECODER_STATUS_SUCCESS;
               gst_buffer_pool_release_buffer((GstBufferPool *)pool, buffer);
	   }
       }
       if(pool)
           gst_object_unref(GST_OBJECT(pool));	
    }
#endif
    return status;
}
