/*
 *  gstvaapidecode.c - VA-API video decoder
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
 * SECTION:gstvaapidecode
 * @short_description: A VA-API based video decoder
 *
 * vaapidecode decodes from raw bitstreams to surfaces suitable for
 * the vaapisink element.
 */

#include "config.h"

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapivideosink.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/video/videocontext.h>

#if USE_VAAPI_GLX
#include <gst/vaapi/gstvaapivideobuffer_glx.h>
#define gst_vaapi_video_buffer_new(display) \
    gst_vaapi_video_buffer_glx_new(GST_VAAPI_DISPLAY_GLX(display))
#endif

#include "gstvaapidecode.h"
#include "gstvaapipluginutil.h"

#if USE_FFMPEG
# include <gst/vaapi/gstvaapidecoder_ffmpeg.h>
#endif
#if USE_CODEC_PARSERS
//# include <gst/vaapi/gstvaapidecoder_h264.h>
//# include <gst/vaapi/gstvaapidecoder_jpeg.h>
# include <gst/vaapi/gstvaapidecoder_mpeg2.h>
//# include <gst/vaapi/gstvaapidecoder_mpeg4.h>
//# include <gst/vaapi/gstvaapidecoder_vc1.h>
#endif

/* Favor codecparsers-based decoders for 0.3.x series */
#define USE_FFMPEG_DEFAULT \
    (USE_FFMPEG && !USE_CODEC_PARSERS)

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidecode);
#define GST_CAT_DEFAULT gst_debug_vaapidecode

/* ElementFactory information */
static const GstElementDetails gst_vaapidecode_details =
    GST_ELEMENT_DETAILS(
        "VA-API decoder",
        "Codec/Decoder/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

/* Default templates */
#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapidecode_sink_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=2, systemstream=(boolean)false")
    GST_CAPS_CODEC("video/mpeg, mpegversion=4")
    GST_CAPS_CODEC("video/x-divx")
    GST_CAPS_CODEC("video/x-xvid")
    GST_CAPS_CODEC("video/x-h263")
    GST_CAPS_CODEC("video/x-h264")
    GST_CAPS_CODEC("video/x-wmv")
    GST_CAPS_CODEC("image/jpeg")
    ;

static const char gst_vaapidecode_src_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;

static GstStaticPadTemplate gst_vaapidecode_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_sink_caps_str));

static GstStaticPadTemplate gst_vaapidecode_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_src_caps_str));

#define GstVideoContextClass GstVideoContextInterface
GST_BOILERPLATE_WITH_INTERFACE(
    GstVaapiDecode,
    gst_vaapidecode,
    GstVideoDecoder,
    GST_TYPE_VIDEO_DECODER,
    GstVideoContext,
    GST_TYPE_VIDEO_CONTEXT,
    gst_video_context);

enum {
    PROP_0,

    PROP_USE_FFMPEG,
};

static gboolean gst_vaapi_dec_open (GstVideoDecoder * decoder);
static gboolean gst_vaapi_dec_start (GstVideoDecoder * decoder);
static gboolean gst_vaapi_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_vaapi_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_vaapi_dec_reset (GstVideoDecoder * decoder, gboolean hard);
static GstFlowReturn gst_vaapi_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static GstFlowReturn gst_vaapi_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode, GstCaps *caps);

static void
gst_vaapi_decoder_notify_caps(GObject *obj, GParamSpec *pspec, void *user_data)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(user_data);
    GstCaps *caps;

    g_assert(decode->decoder == GST_VAAPI_DECODER(obj));

    caps = gst_vaapi_decoder_get_caps(decode->decoder);
    gst_vaapidecode_update_src_caps(decode, caps);
}

static inline gboolean
gst_vaapidecode_update_sink_caps(GstVaapiDecode *decode, GstCaps *caps)
{
    if (decode->sinkpad_caps)
        gst_caps_unref(decode->sinkpad_caps);
    decode->sinkpad_caps = gst_caps_ref(caps);
    return TRUE;
}

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode, GstCaps *caps)
{
    GstStructure *structure;
    const GValue *v_width, *v_height, *v_framerate, *v_par, *v_interlaced;
    gboolean success = TRUE;

    if (!decode->srcpad_caps) {
        decode->srcpad_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS_NAME);
        if (!decode->srcpad_caps)
            return FALSE;
    }

    structure    = gst_caps_get_structure(caps, 0);
    v_width      = gst_structure_get_value(structure, "width");
    v_height     = gst_structure_get_value(structure, "height");
    v_framerate  = gst_structure_get_value(structure, "framerate");
    v_par        = gst_structure_get_value(structure, "pixel-aspect-ratio");
    v_interlaced = gst_structure_get_value(structure, "interlaced");

    structure = gst_caps_get_structure(decode->srcpad_caps, 0);
    if (v_width && v_height) {
        gst_structure_set_value(structure, "width", v_width);
        gst_structure_set_value(structure, "height", v_height);
    }
    if (v_framerate)
        gst_structure_set_value(structure, "framerate", v_framerate);
    if (v_par)
        gst_structure_set_value(structure, "pixel-aspect-ratio", v_par);
    if (v_interlaced)
        gst_structure_set_value(structure, "interlaced", v_interlaced);

    gst_structure_set(structure, "type", G_TYPE_STRING, "vaapi", NULL);
    gst_structure_set(structure, "opengl", G_TYPE_BOOLEAN, USE_VAAPI_GLX, NULL);

    return success;
}

static void
gst_vaapidecode_release(GstVaapiDecode *decode, GObject *dead_object)
{
    g_mutex_lock(decode->decoder_mutex);
    g_cond_signal(decode->decoder_ready);
    g_mutex_unlock(decode->decoder_mutex);
}

static GstFlowReturn
gst_vaapidecode_step(GstVaapiDecode *decode)
{
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;
    GstBuffer *buffer;
    GstFlowReturn ret;
    guint tries;

    for (;;) {
        tries = 0;
    again:
        proxy = gst_vaapi_decoder_get_surface(decode->decoder, &status);
        if (!proxy) {
            if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE) {
                /* Wait for a VA surface to be displayed and free'd */
                if (++tries > 100)
                    goto error_decode_timeout;
                GTimeVal timeout;
                g_get_current_time(&timeout);
                g_time_val_add(&timeout, 10000); /* 10 ms each step */
                g_mutex_lock(decode->decoder_mutex);
                g_cond_timed_wait(
                    decode->decoder_ready,
                    decode->decoder_mutex,
                    &timeout
                );
                g_mutex_unlock(decode->decoder_mutex);
                goto again;
            }
            if (status != GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA)
                goto error_decode;
            /* More data is needed */
            break;
        }

        g_object_weak_ref(
            G_OBJECT(proxy),
            (GWeakNotify)gst_vaapidecode_release,
            decode
        );

        buffer = gst_vaapi_video_buffer_new(decode->display);
        if (!buffer)
            goto error_create_buffer;

        GST_BUFFER_TIMESTAMP(buffer) = GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy);
        //gst_buffer_set_caps(buffer, GST_PAD_CAPS(decode->srcpad));

        if (GST_VAAPI_SURFACE_PROXY_TFF(proxy))
            GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_TFF);

        gst_vaapi_video_buffer_set_surface_proxy(
            GST_VAAPI_VIDEO_BUFFER(buffer),
            proxy
        );

        //ret = gst_pad_push(decode->srcpad, buffer);
        if (ret != GST_FLOW_OK)
            goto error_commit_buffer;

        g_object_unref(proxy);
    }
    return GST_FLOW_OK;

    /* ERRORS */
error_decode_timeout:
    {
        GST_DEBUG("decode timeout. Decoder required a VA surface but none "
                  "got available within one second");
        return GST_FLOW_UNEXPECTED;
    }
error_decode:
    {
        GST_DEBUG("decode error %d", status);
        switch (status) {
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
            ret = GST_FLOW_NOT_SUPPORTED;
            break;
        default:
            ret = GST_FLOW_UNEXPECTED;
            break;
        }
        return ret;
    }
error_create_buffer:
    {
        const GstVaapiID surface_id =
            gst_vaapi_surface_get_id(GST_VAAPI_SURFACE_PROXY_SURFACE(proxy));

        GST_DEBUG("video sink failed to create video buffer for proxy'ed "
                  "surface %" GST_VAAPI_ID_FORMAT " (error %d)",
                  GST_VAAPI_ID_ARGS(surface_id), ret);
        g_object_unref(proxy);
        return GST_FLOW_UNEXPECTED;
    }
error_commit_buffer:
    {
        GST_DEBUG("video sink rejected the video buffer (error %d)", ret);
        g_object_unref(proxy);
        return GST_FLOW_UNEXPECTED;
    }
}

static gboolean
gst_vaapidecode_create(GstVaapiDecode *decode, GstCaps *caps)
{
    GstVaapiDisplay *dpy;
    GstStructure *structure;
    int version;

    if (!gst_vaapi_ensure_display(decode, &decode->display))
        return FALSE;

    decode->decoder_mutex = g_mutex_new();
    if (!decode->decoder_mutex)
        return FALSE;

    decode->decoder_ready = g_cond_new();
    if (!decode->decoder_ready)
        return FALSE;

    dpy = decode->display;
    if (decode->use_ffmpeg) {
#if USE_FFMPEG
        decode->decoder = gst_vaapi_decoder_ffmpeg_new(dpy, caps);
#endif
    }
    else {
#if USE_CODEC_PARSERS
        structure = gst_caps_get_structure(caps, 0);
        if (!structure)
            return FALSE;
      /*  if (gst_structure_has_name(structure, "video/x-h264"))
            decode->decoder = gst_vaapi_decoder_h264_new(dpy, caps);
        else*/ if (gst_structure_has_name(structure, "video/mpeg")) {
            if (!gst_structure_get_int(structure, "mpegversion", &version))
                return FALSE;
            if (version == 2)
                decode->decoder = gst_vaapi_decoder_mpeg2_new(dpy, caps);
            /*else if (version == 4)
                decode->decoder = gst_vaapi_decoder_mpeg4_new(dpy, caps);*/
        }
        /*else if (gst_structure_has_name(structure, "video/x-wmv"))
            decode->decoder = gst_vaapi_decoder_vc1_new(dpy, caps);
        else if (gst_structure_has_name(structure, "video/x-h263") ||
                 gst_structure_has_name(structure, "video/x-divx") ||
                 gst_structure_has_name(structure, "video/x-xvid"))
            decode->decoder = gst_vaapi_decoder_mpeg4_new(dpy, caps);
#if USE_JPEG_DECODER
        else if (gst_structure_has_name(structure, "image/jpeg"))
            decode->decoder = gst_vaapi_decoder_jpeg_new(dpy, caps);
#endif */
#endif
    }
    if (!decode->decoder)
        return FALSE;

    g_signal_connect(
        G_OBJECT(decode->decoder),
        "notify::caps",
        G_CALLBACK(gst_vaapi_decoder_notify_caps),
        decode
    );

    decode->decoder_caps = gst_caps_ref(caps);
    return TRUE;
}

static void
gst_vaapidecode_destroy(GstVaapiDecode *decode)
{
    if (decode->decoder) {
        gst_vaapi_decoder_put_buffer(decode->decoder, NULL);
        g_object_unref(decode->decoder);
        decode->decoder = NULL;
    }

    if (decode->decoder_caps) {
        gst_caps_unref(decode->decoder_caps);
        decode->decoder_caps = NULL;
    }

    if (decode->decoder_ready) {
        gst_vaapidecode_release(decode, NULL);
        g_cond_free(decode->decoder_ready);
        decode->decoder_ready = NULL;
    }

    if (decode->decoder_mutex) {
        g_mutex_free(decode->decoder_mutex);
        decode->decoder_mutex = NULL;
    }
}

static gboolean
gst_vaapidecode_reset(GstVaapiDecode *decode, GstCaps *caps)
{
    if (decode->decoder &&
        decode->decoder_caps &&
        gst_caps_is_always_compatible(caps, decode->decoder_caps))
        return TRUE;

    gst_vaapidecode_destroy(decode);
    return gst_vaapidecode_create(decode, caps);
}

/* GstVideoContext interface */

static void
gst_vaapidecode_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
    GstVaapiDecode *decode = GST_VAAPIDECODE (context);
    gst_vaapi_set_display (type, value, &decode->display);
}

static gboolean
gst_video_context_supported (GstVaapiDecode *decode, GType iface_type)
{
  return (iface_type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapidecode_set_video_context;
}

static void
gst_vaapidecode_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstPadTemplate *pad_template;

    gst_element_class_set_details(element_class, &gst_vaapidecode_details);

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);
}

static void
gst_vaapidecode_finalize(GObject *object)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    gst_vaapidecode_destroy(decode);

    if (decode->sinkpad_caps) {
        gst_caps_unref(decode->sinkpad_caps);
        decode->sinkpad_caps = NULL;
    }

    if (decode->srcpad_caps) {
        gst_caps_unref(decode->srcpad_caps);
        decode->srcpad_caps = NULL;
    }

    if (decode->display) {
        g_object_unref(decode->display);
        decode->display = NULL;
    }

    if (decode->allowed_caps) {
        gst_caps_unref(decode->allowed_caps);
        decode->allowed_caps = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vaapidecode_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    switch (prop_id) {
    case PROP_USE_FFMPEG:
        decode->use_ffmpeg = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapidecode_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    switch (prop_id) {
    case PROP_USE_FFMPEG:
        g_value_set_boolean(value, decode->use_ffmpeg);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapidecode_class_init(GstVaapiDecodeClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidecode,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize      = gst_vaapidecode_finalize;
    object_class->set_property  = gst_vaapidecode_set_property;
    object_class->get_property  = gst_vaapidecode_get_property;

#if USE_FFMPEG
    g_object_class_install_property
        (object_class,
         PROP_USE_FFMPEG,
         g_param_spec_boolean("use-ffmpeg",
                              "Use FFmpeg/VAAPI for decoding",
                              "Uses FFmpeg/VAAPI for decoding",
                              USE_FFMPEG_DEFAULT,
                              G_PARAM_READWRITE));
#endif

    video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_vaapi_dec_open);
    video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_vaapi_dec_start);
    video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vaapi_dec_stop);
    video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_vaapi_dec_reset);
    video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vaapi_dec_set_format);
    video_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_vaapi_dec_parse);
    video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vaapi_dec_handle_frame);
    
}

static gboolean
gst_vaapidecode_ensure_allowed_caps(GstVaapiDecode *decode)
{
    GstCaps *decode_caps;
    guint i, n_decode_caps;

    if (decode->allowed_caps)
        return TRUE;

    if (!gst_vaapi_ensure_display(decode, &decode->display))
        goto error_no_display;

    decode_caps = gst_vaapi_display_get_decode_caps(decode->display);
    if (!decode_caps)
        goto error_no_decode_caps;
    n_decode_caps = gst_caps_get_size(decode_caps);

    decode->allowed_caps = gst_caps_new_empty();
    if (!decode->allowed_caps)
        goto error_no_memory;

    for (i = 0; i < n_decode_caps; i++) {
        GstStructure *structure;
        structure = gst_caps_get_structure(decode_caps, i);
        if (!structure)
            continue;
        structure = gst_structure_copy(structure);
        if (!structure)
            continue;
        gst_structure_remove_field(structure, "profile");
        gst_structure_set(
            structure,
            "width",  GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            NULL
        );
        gst_caps_merge_structure(decode->allowed_caps, structure);
    }

    gst_caps_unref(decode_caps);
    return TRUE;

    /* ERRORS */
error_no_display:
    {
        GST_DEBUG("failed to retrieve VA display");
        return FALSE;
    }
error_no_decode_caps:
    {
        GST_DEBUG("failed to retrieve VA decode caps");
        return FALSE;
    }
error_no_memory:
    {
        GST_DEBUG("failed to allocate allowed-caps set");
        gst_caps_unref(decode_caps);
        return FALSE;
    }
}

static GstCaps *
gst_vaapidecode_get_caps(GstPad *pad)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    if (!decode->is_ready)
        return gst_static_pad_template_get_caps(&gst_vaapidecode_sink_factory);

    if (!gst_vaapidecode_ensure_allowed_caps(decode))
        return gst_caps_new_empty();

    return gst_caps_ref(decode->allowed_caps);
}

static gboolean
gst_vaapidecode_query (GstPad *pad, GstQuery *query) {
    GstVaapiDecode *decode = GST_VAAPIDECODE (gst_pad_get_parent_element (pad));
    gboolean res;
    GstPadQueryFunction func;

    GST_DEBUG ("sharing display %p", decode->display);
/*Fixme*/
    if (gst_vaapi_reply_to_query (query, decode->display))
       res = TRUE;
    else {
      if (GST_PAD_IS_SINK(pad)) {
        func   = GST_PAD_QUERYFUNC (GST_VIDEO_DECODER_SINK_PAD(decode));
	res = func (pad, query);
      } else {
        func   = GST_PAD_QUERYFUNC (GST_VIDEO_DECODER_SRC_PAD(decode));
	res = func (pad, query);
      }
    }

    g_object_unref (decode);
    return res;
}

static void
gst_vaapidecode_init(GstVaapiDecode *decode, GstVaapiDecodeClass *klass)
{
    decode->display             = NULL;
    decode->decoder             = NULL;
    decode->decoder_mutex       = NULL;
    decode->decoder_ready       = NULL;
    decode->use_ffmpeg          = USE_FFMPEG_DEFAULT;
    decode->is_ready            = FALSE;
    decode->sinkpad_caps        = NULL;
    decode->srcpad_caps         = NULL;
    decode->decoder_caps        = NULL;
    decode->allowed_caps        = NULL;

//  gst_pad_set_query_function(GST_VIDEO_DECODER_SINK_PAD(decode), gst_vaapidecode_query);
//  gst_pad_set_query_function(GST_VIDEO_DECODER_SRC_PAD(decode), gst_vaapidecode_query); 
 
    gst_pad_set_getcaps_function(GST_VIDEO_DECODER (decode)->sinkpad, gst_vaapidecode_get_caps); 
    gst_video_decoder_set_packetized (GST_VIDEO_DECODER(decode), FALSE);
    
}

static gboolean
gst_vaapi_dec_open (GstVideoDecoder *decoder)
{
  GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);
  GST_DEBUG_OBJECT (decoder, "open");
  dec->is_ready = TRUE;
  return TRUE;
}

static gboolean
gst_vaapi_dec_start (GstVideoDecoder * decoder)
{
  GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);

  GST_DEBUG_OBJECT (decoder, "start");
  
  return TRUE;
}

static gboolean
gst_vaapi_dec_stop (GstVideoDecoder * decoder)
{
  GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  gst_vaapidecode_destroy(dec);
        if (dec->display) {
            g_object_unref(dec->display);
            dec->display = NULL;
        }
  dec->is_ready = FALSE;

  return TRUE;
}

static gboolean
gst_vaapi_dec_reset (GstVideoDecoder * bdec, gboolean hard)
{
  GstVaapiDecode *dec = GST_VAAPIDECODE (bdec);
  
  GST_DEBUG_OBJECT (dec, "reset");

  return TRUE;
}

static gboolean
gst_vaapi_dec_set_format (GstVideoDecoder * bdec, GstVideoCodecState * state)
{
  GstVaapiDecode *dec;
  GstCaps *caps;
  GstVideoInfo info;
  guint width, height;
  GstVaapiDisplay *dpy;
  GstStructure *structure;
  GstVideoFormat fmt;

  dec = GST_VAAPIDECODE (bdec);

  if (!state)
    return FALSE;

 
  /* Keep a copy of the input state */
  if (dec->input_state)
    gst_video_codec_state_unref (dec->input_state);
  dec->input_state = gst_video_codec_state_ref (state);

  info = state->info;
  caps = state->caps;
  width = info.width;
  height = info.height;
  fmt = GST_VIDEO_FORMAT_YV12;

  if (!gst_vaapidecode_update_sink_caps(dec, caps))
        return FALSE;

  if (!gst_vaapidecode_update_src_caps(dec, caps))
        return FALSE;

  if (!gst_vaapidecode_reset(dec, dec->sinkpad_caps))
        return FALSE;

  /*Fixme: set output state after seq_hdr parsing*/
  dec->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec), fmt,
      info.width, info.height, dec->input_state);

  dec->output_state->caps = dec->srcpad_caps;
  
}

static GstFlowReturn
gst_vaapi_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  GstVaapiDecode *dec;
  GstVaapiDecoderStatus status;
  GstFlowReturn ret = GST_FLOW_OK;
  gint toadd;
  /*Fixme : check at_eos*/
  dec = GST_VAAPIDECODE (decoder);
  
  status = GST_VAAPI_DECODER_GET_CLASS(dec->decoder)->parse (dec->decoder, adapter, &toadd);
  /* if parse() returns GST_VAAPI_DECODER_STATUS_SUCCESS,
	case 1: toadd = 0  : got the full frame to decode, call finish_frame
	case 2: toadd != 0 : add data to frame, by calling add_to_frame */
  if (status == GST_VAAPI_DECODER_STATUS_SUCCESS) {
      if (toadd) {
         gst_video_decoder_add_to_frame (decoder, toadd);
	 goto beach;
      }
      else {
         ret = gst_video_decoder_have_frame (decoder);
	 goto beach;
      }
  } else if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA) {
       ret = GST_VIDEO_DECODER_FLOW_NEED_DATA;
       goto beach;
  } else
     goto error_decode;

error_decode:
  {
        GST_DEBUG("parse/decode error %d", status);
        switch (status) {
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
            ret = GST_FLOW_NOT_SUPPORTED;
            break;
        default:
            ret = GST_FLOW_UNEXPECTED;
            break;
        }
  }
beach:
  return ret;
}

static GstFlowReturn
gst_vaapi_dec_handle_frame (GstVideoDecoder * bdec, GstVideoCodecFrame * frame)
{
  GstVaapiDecode *dec;
  GstVideoCodecFrame *frame_out = NULL;
  gint frame_id = -1;
  GstVaapiSurfaceProxy *proxy;
  GstBuffer *buffer;
  GstFlowReturn ret=GST_FLOW_OK;

  dec = GST_VAAPIDECODE (bdec);

  ret = GST_VAAPI_DECODER_GET_CLASS(dec->decoder)->decode(dec->decoder, frame);

  if (ret == GST_FLOW_OK) {
    proxy = gst_vaapi_decoder_get_surface_proxy (dec->decoder);

    if (proxy) {
        buffer = gst_vaapi_video_buffer_new(dec->display);
	if (!buffer)
            goto error_create_buffer;

        GST_BUFFER_TIMESTAMP(buffer) = GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy);

        if (GST_VAAPI_SURFACE_PROXY_TFF(proxy))
            GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_TFF);

        gst_vaapi_video_buffer_set_surface_proxy(
            GST_VAAPI_VIDEO_BUFFER(buffer),
            proxy
        );

       frame_id  = gst_vaapi_surface_proxy_get_frame_id (proxy);
       frame_out = gst_video_decoder_get_frame (bdec, frame_id);
       frame_out->output_buffer = buffer;
       frame_out->pts = GST_BUFFER_TIMESTAMP(buffer);
       ret = gst_video_decoder_finish_frame (bdec, frame_out);
       if (ret != GST_FLOW_OK)
         goto error_commit_buffer;		

       /*GstVideoDecoder creating the subbuffer which will create an extra ref to parent buffer*/
       gst_buffer_unref (frame_out->output_buffer);
       g_object_unref (proxy);
      } 
  } 

  return ret;
 
  error_create_buffer:
  {
        const GstVaapiID surface_id =
            gst_vaapi_surface_get_id(GST_VAAPI_SURFACE_PROXY_SURFACE(proxy));

        GST_DEBUG("video sink failed to create video buffer for proxy'ed "
                  "surface %" GST_VAAPI_ID_FORMAT " (error %d)",
                  GST_VAAPI_ID_ARGS(surface_id), ret);
        g_object_unref(proxy);
        return GST_FLOW_UNEXPECTED;
  }
 
  error_commit_buffer:
  {
        GST_DEBUG("video sink rejected the video buffer (error %d)", ret);
        g_object_unref(proxy);
        return GST_FLOW_UNEXPECTED;
  }
}
  

