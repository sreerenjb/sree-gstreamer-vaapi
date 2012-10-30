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

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/video/videocontext.h>

#include "gstvaapidecode.h"
#include "gstvaapipluginutil.h"

# include <gst/vaapi/gstvaapidecoder_h264.h>
# include <gst/vaapi/gstvaapidecoder_jpeg.h>
# include <gst/vaapi/gstvaapidecoder_mpeg2.h>
//# include <gst/vaapi/gstvaapidecoder_mpeg4.h>
# include <gst/vaapi/gstvaapidecoder_vc1.h>

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidecode);
#define GST_CAT_DEFAULT gst_debug_vaapidecode

/* Default templates */
#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapidecode_sink_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=[1, 2], systemstream=(boolean)false")
    GST_CAPS_CODEC("video/mpeg, mpegversion=4")
    GST_CAPS_CODEC("video/x-divx")
    GST_CAPS_CODEC("video/x-xvid")
    GST_CAPS_CODEC("video/x-h263")
    GST_CAPS_CODEC("video/x-h264")
    GST_CAPS_CODEC("video/x-wmv")
    GST_CAPS_CODEC("image/jpeg")
    ;

/*static const char gst_vaapidecode_src_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;*/

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
        GST_STATIC_CAPS("video/x-raw"));

static void
gst_video_context_interface_init(GstVideoContextInterface *iface);

static void 
gst_vaapi_dec_negotiate(GstVideoDecoder *dec);

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiDecode,
    gst_vaapidecode,
    GST_TYPE_VIDEO_DECODER,
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

static gboolean gst_vaapi_dec_open (GstVideoDecoder * decoder);
static gboolean gst_vaapi_dec_start (GstVideoDecoder * decoder);
static gboolean gst_vaapi_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_vaapi_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_vaapi_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
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
    /*negotiate with downstream once the caps property of GstVideoDecoder has changed*/
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
    GstCaps *other_caps;
    const GValue *v_width, *v_height, *v_framerate, *v_par, *v_interlace_mode;
    gboolean success = TRUE;

    if (!decode->srcpad_caps) {
        decode->srcpad_caps = gst_caps_from_string("video/x-raw");
        if (!decode->srcpad_caps)
            return FALSE;
    }
    
    structure    = gst_caps_get_structure(caps, 0);
    v_width      = gst_structure_get_value(structure, "width");
    v_height     = gst_structure_get_value(structure, "height");
    v_framerate  = gst_structure_get_value(structure, "framerate");
    v_par        = gst_structure_get_value(structure, "pixel-aspect-ratio");
    v_interlace_mode = gst_structure_get_value(structure, "interlace-mode");

    other_caps = decode->srcpad_caps;
    decode->srcpad_caps = gst_caps_copy(decode->srcpad_caps);
    gst_caps_unref(other_caps);

    structure = gst_caps_get_structure(decode->srcpad_caps, 0); 
    if (v_width && v_height) {
        gst_structure_set_value(structure, "width", v_width);
        gst_structure_set_value(structure, "height", v_height);
    }
    if (v_framerate)
        gst_structure_set_value(structure, "framerate", v_framerate);
    if (v_par)
        gst_structure_set_value(structure, "pixel-aspect-ratio", v_par);
    
    /*Fixme: setting a new "va-interlace-mode" to get the interlacing working with vaapipostproc element*/
    if (v_interlace_mode) 
        gst_structure_set_value(structure, "va-interlace-mode", v_interlace_mode);
    else
        gst_structure_set(structure, "va-interlace-mode", G_TYPE_STRING, "progressive", NULL);

    /*Fixme: setting interlace-mode to progressive , otherwise deinterlace element which is autoplugging 
	     in playbin will do the deinterlacing which will end up with unnecessary vaSurface mapping. */
    gst_structure_set(structure, "interlace-mode", G_TYPE_STRING, "progressive", NULL);
    /*Fixme: setting format to NV12 for now*/
    gst_structure_set(structure, "format", G_TYPE_STRING, "NV12", NULL); /*Fixme*/
    gst_structure_set(structure, "type", G_TYPE_STRING, "vaapi", NULL);
    gst_structure_set(structure, "opengl", G_TYPE_BOOLEAN, USE_GLX, NULL);

    return TRUE;
}

static inline gboolean
gst_vaapidecode_ensure_display(GstVaapiDecode *decode)
{
    return gst_vaapi_ensure_display(decode, GST_VAAPI_DISPLAY_TYPE_ANY,
        &decode->display);
}

static inline guint
gst_vaapi_codec_from_caps(GstCaps *caps)
{
    return gst_vaapi_profile_get_codec(gst_vaapi_profile_from_caps(caps));
}

static gboolean
gst_vaapidecode_create(GstVaapiDecode *decode, GstCaps *caps)
{
    GstVaapiDisplay *dpy;

    if (!gst_vaapidecode_ensure_display(decode))
        return FALSE;
    dpy = decode->display;

    switch (gst_vaapi_codec_from_caps(caps)) {
    case GST_VAAPI_CODEC_MPEG2:
        decode->decoder = gst_vaapi_decoder_mpeg2_new(dpy, caps);
        break;
#if 0
    case GST_VAAPI_CODEC_MPEG4:
    case GST_VAAPI_CODEC_H263:
        decode->decoder = gst_vaapi_decoder_mpeg4_new(dpy, caps);
        break;
#endif
    case GST_VAAPI_CODEC_H264:
        decode->decoder = gst_vaapi_decoder_h264_new(dpy, caps);
        break;
    case GST_VAAPI_CODEC_WMV3:
    case GST_VAAPI_CODEC_VC1:
        decode->decoder = gst_vaapi_decoder_vc1_new(dpy, caps);
        break;
#if USE_JPEG_DECODER
    case GST_VAAPI_CODEC_JPEG:
        decode->decoder = gst_vaapi_decoder_jpeg_new(dpy, caps);
        break;
#endif
    default:
        decode->decoder = NULL;
        break;
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
        g_object_unref(decode->decoder);
        decode->decoder = NULL;
    }
    if (decode->decoder_caps) {
        gst_caps_unref(decode->decoder_caps);
        decode->decoder_caps = NULL;
    }
}

static gboolean
gst_vaapidecode_reset(GstVaapiDecode *decode, GstCaps *caps)
{
    GstVaapiCodec codec;

    /* Only reset decoder if codec type changed */
    if (decode->decoder && decode->decoder_caps) {
        if (gst_caps_is_always_compatible(caps, decode->decoder_caps))
            return TRUE;
        codec = gst_vaapi_codec_from_caps(caps);
        if (codec == gst_vaapi_decoder_get_codec(decode->decoder))
            return TRUE;
    }

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

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapidecode_set_video_context;
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

    g_clear_object(&decode->display);

    if (decode->allowed_caps) {
        gst_caps_unref(decode->allowed_caps);
        decode->allowed_caps = NULL;
    }

    G_OBJECT_CLASS(gst_vaapidecode_parent_class)->finalize(object);
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
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidecode,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize      = gst_vaapidecode_finalize;

    video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_vaapi_dec_open);
    video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_vaapi_dec_start);
    video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vaapi_dec_stop);
    video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_vaapi_dec_reset);
    video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vaapi_dec_set_format);
    video_decoder_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_vaapi_dec_decide_allocation);
    video_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_vaapi_dec_parse);
    video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vaapi_dec_handle_frame);
   
    gst_element_class_set_static_metadata (element_class,
      "VA-API decoder",
      "Codec/Decoder/Video", GST_PLUGIN_DESC,
      "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

   /* sink pad */
   gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_vaapidecode_sink_factory));

   /* src pad */
   gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&gst_vaapidecode_src_factory));
}

/* Fixme? : No more get_caps..Is there any need to test the allowded caps(allowded by current display) ? */
#if 0
static gboolean
gst_vaapidecode_ensure_allowed_caps(GstVaapiDecode *decode)
{
    GstCaps *decode_caps;
    guint i, n_decode_caps;

    if (decode->allowed_caps)
        return TRUE;

    if (!gst_vaapidecode_ensure_display(decode))
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
gst_vaapidecode_get_caps(GstVaapiDecode *decode, GstPad *pad, GstCaps *filter)
{
    if (!decode->is_ready)
        return gst_static_pad_template_get_caps(&gst_vaapidecode_sink_factory);

    if (!gst_vaapidecode_ensure_allowed_caps(decode))
        return gst_caps_new_empty();

    return gst_caps_ref(decode->allowed_caps);
}
#endif

static gboolean
gst_vaapidecode_query (GstPad *pad, GstObject *parent, GstQuery *query) {
    GstVaapiDecode *decode = GST_VAAPIDECODE (parent);
    GstVideoDecoder *bdec  = GST_VIDEO_DECODER (decode);
    gboolean res = TRUE;

    GST_DEBUG ("sharing display %p", decode->display);

    if (gst_vaapi_reply_to_query (query, decode->display))
       res = TRUE;
    else {
      if (GST_PAD_IS_SINK(pad))
        res = decode->sinkpad_qfunc (GST_VIDEO_DECODER_SINK_PAD(bdec), parent,  query);
      else
        res = decode->srcpad_qfunc (GST_VIDEO_DECODER_SRC_PAD(bdec), parent, query);
    }
    return res;
}

static void
gst_vaapidecode_init(GstVaapiDecode *decode)
{
    GstVideoDecoder *bdec       = GST_VIDEO_DECODER (decode);
    decode->display             = NULL;
    decode->decoder             = NULL;
    decode->is_ready            = FALSE;
    decode->sinkpad_caps        = NULL;
    decode->srcpad_caps         = NULL;
    decode->decoder_caps        = NULL;
    decode->allowed_caps        = NULL;

    decode->sinkpad_qfunc       = GST_PAD_QUERYFUNC (GST_VIDEO_DECODER_SINK_PAD(bdec));
    decode->srcpad_qfunc        = GST_PAD_QUERYFUNC (GST_VIDEO_DECODER_SRC_PAD(bdec));

    gst_pad_set_query_function(GST_VIDEO_DECODER_SINK_PAD(decode), gst_vaapidecode_query);
    gst_pad_set_query_function(GST_VIDEO_DECODER_SRC_PAD(decode), gst_vaapidecode_query); 
 
    gst_video_decoder_set_packetized (GST_VIDEO_DECODER(decode), FALSE);
}

static gboolean
gst_vaapi_dec_open(GstVideoDecoder *decoder)
{
    GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);
    GST_DEBUG_OBJECT (decoder, "open");
    dec->is_ready = TRUE;
    return TRUE;
}

static gboolean
gst_vaapi_dec_start(GstVideoDecoder * decoder)
{
    GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);
    gboolean ret = TRUE;

    GST_DEBUG_OBJECT (decoder, "start");
    if (!gst_vaapi_decoder_start(dec->decoder)) {
	GST_ERROR_OBJECT(decoder, "Failed to start the processing..");
	ret = FALSE;
    }

    return ret;
}

static gboolean
gst_vaapi_dec_stop(GstVideoDecoder * decoder)
{
    GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);
    gboolean ret = TRUE; 
    
    GST_DEBUG_OBJECT (dec, "stop");

    if (!gst_vaapi_decoder_stop(dec->decoder)) {
	GST_ERROR_OBJECT(decoder, "Failed to stop processing,,");
	ret = FALSE;
    }
    dec->is_ready = FALSE;

    return ret;
}

/*To perform post-seek semantics reset*/
static gboolean
gst_vaapi_dec_reset(GstVideoDecoder * bdec, gboolean hard)
{
    GstVaapiDecode *dec = GST_VAAPIDECODE (bdec);
  
    GST_DEBUG_OBJECT (dec, "reset");
    if(dec->decoder) {
        if(!gst_vaapi_decoder_reset(dec->decoder)){
	    GST_ERROR_OBJECT(dec, "Failed to reset : seeking might fail..");
	    return FALSE;
	}
    }
    return TRUE;
}

static gboolean
gst_vaapi_dec_set_format(GstVideoDecoder * bdec, GstVideoCodecState * state)
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

    if (!gst_vaapidecode_update_sink_caps(dec, caps))
        return FALSE;

    if (!gst_vaapidecode_update_src_caps(dec, caps))
        return FALSE;

    if (!gst_vaapidecode_reset(dec, dec->sinkpad_caps))
        return FALSE;
    
    /*Fixme: add codec_dat hadling from state->codec_data*/
}

static GstFlowReturn
gst_vaapi_dec_parse(GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
    GstVaapiDecode *dec;
    GstVaapiDecoderStatus status;
    GstFlowReturn ret = GST_FLOW_OK;
    gint toadd;
    gboolean have_frame = FALSE;

    dec = GST_VAAPIDECODE (decoder);
    if (at_eos) {
        GST_DEBUG_OBJECT (dec, "Stop parsing and render the pending frames");
        gst_video_decoder_add_to_frame(decoder, gst_adapter_available (adapter));
        ret = gst_video_decoder_have_frame(decoder);
	goto beach;
    }

    status = gst_vaapi_decoder_parse(dec->decoder, adapter, &toadd, &have_frame);
    /* if parse() returns GST_VAAPI_DECODER_STATUS_SUCCESS,
  	-- size we got in #toadd is to be added to #frame
	-- send the data for decoding if #have_frame is TRUE */
    if (status == GST_VAAPI_DECODER_STATUS_SUCCESS) {
        
 	if(toadd) 
            gst_video_decoder_add_to_frame(decoder, toadd);
       	if(have_frame)
            ret = gst_video_decoder_have_frame(decoder);
 	goto beach; 

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
             ret = GST_FLOW_EOS;
             break;
         }
    }   
beach: 
    return ret;
}

static gboolean
gst_vaapi_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
    GstVaapiDecode *dec = GST_VAAPIDECODE (decoder);
    GstVideoCodecState *state;
    GstBufferPool *pool;
    guint size, min, max;
    GstStructure *config, *structure;
    GstVideoInfo info;
    GstCaps *caps;
    gboolean ret;
    gboolean need_pool;

    gst_query_parse_allocation (query, &caps, &need_pool);

    if (need_pool) {
        ret = gst_vaapi_decoder_decide_allocation (dec->decoder, query);
        if (!ret) {
            GST_DEBUG_OBJECT (dec, "Failed to allocate buffer pool..");
            return FALSE;
        }
    }
    return  TRUE;
}

static void
gst_vaapi_dec_negotiate(GstVideoDecoder *dec)
{ 
    GstVaapiDecode *vaapi_dec;
    GstVideoCodecState *outstate;
    GstVideoInfo srcpad_info, outstate_info;
    GstVideoFormat format;
    GstCaps *caps;

    vaapi_dec = GST_VAAPIDECODE(dec);
    
    if(vaapi_dec->srcpad_caps)   
        gst_video_info_from_caps(&srcpad_info, vaapi_dec->srcpad_caps);

    outstate = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec));
    if (outstate) {
        gst_video_info_from_caps(&outstate_info, outstate->caps);
        if (srcpad_info.width  		        == outstate_info.width &&
            srcpad_info.height 		        == outstate_info.height &&
	    GST_VIDEO_INFO_FORMAT(&srcpad_info)	== GST_VIDEO_INFO_FORMAT(&outstate_info)) {

	    gst_video_codec_state_unref (outstate);
            return;
        } 
        gst_video_codec_state_unref (outstate);
    }
    /*Fixme: Setting o/p state has no effect now since we manually set the output_state->caps to srcpad_caps */
    vaapi_dec->output_state =
        gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec), GST_VIDEO_FORMAT_NV12,
        srcpad_info.width, srcpad_info.height, vaapi_dec->input_state);

    vaapi_dec->output_state->caps = vaapi_dec->srcpad_caps;
    gst_video_decoder_negotiate (dec); 
}

static GstFlowReturn
gst_vaapi_dec_handle_frame(GstVideoDecoder * bdec, GstVideoCodecFrame * frame)
{
    GstVaapiDecode *dec;
    GstVideoCodecFrame *frame_out = NULL;
    gint frame_id = -1;
    GstVaapiSurfaceProxy *proxy;
    GstBuffer *buffer;
    GstFlowReturn ret = GST_FLOW_OK;
    GstVaapiDecoderStatus status;
    GstVaapiSurfaceMeta *meta;

    dec = GST_VAAPIDECODE(bdec);

    gst_vaapi_dec_negotiate (bdec);
    
    proxy = gst_vaapi_decoder_get_surface(dec->decoder, frame,  &status);

    do {
        if (proxy) {
            buffer = gst_vaapi_surface_proxy_get_surface_buffer(proxy);
	    if (!buffer)
                goto error_create_buffer;

            GST_BUFFER_TIMESTAMP(buffer) = GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy);
 
  	    meta = gst_buffer_get_vaapi_surface_meta (buffer);

	    /*Fixme: add to videometa? */	    
	    if (GST_VAAPI_SURFACE_PROXY_INTERLACED(proxy)) {
		meta->interlaced = TRUE;
	        if (GST_VAAPI_SURFACE_PROXY_TFF(proxy))
	  	    meta->tff = TRUE;
	        else
		    meta->tff = FALSE;
	    } 

            frame_id  = gst_vaapi_surface_proxy_get_frame_id(proxy);
            frame_out = gst_video_decoder_get_frame(bdec, frame_id);
            frame_out->output_buffer = buffer;
            frame_out->pts = GST_BUFFER_TIMESTAMP(buffer);
            ret = gst_video_decoder_finish_frame(bdec, frame_out);
            if (ret != GST_FLOW_OK)
                goto error_commit_buffer;		
            /*gvd_get_frame() creating an extra ref which we need to unref*/
            gst_video_codec_frame_unref (frame_out);
            g_object_unref (proxy);
        } 
        else if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
	    goto error_decode;

        proxy = gst_vaapi_decoder_get_surface(dec->decoder, NULL,  &status);

    }while (proxy); /* to handle SEQUENCE_END, multiple frames might be pending to render*/
   
    return ret;
 
error_decode:
    {
        GST_DEBUG("decode error %d", status);
	return GST_FLOW_EOS;
    }
error_create_buffer:
    {
         const GstVaapiID surface_id =
             gst_vaapi_surface_get_id(GST_VAAPI_SURFACE_PROXY_SURFACE(proxy));

         GST_DEBUG("video sink failed to create video buffer for proxy'ed "
                  "surface %" GST_VAAPI_ID_FORMAT " (error %d)",
                  GST_VAAPI_ID_ARGS(surface_id), ret);
         g_object_unref(proxy);
         return GST_FLOW_EOS;
    }
error_commit_buffer:
    {
        GST_DEBUG("video sink rejected the video buffer (error %d)", ret);
        g_object_unref(proxy);
        return GST_FLOW_EOS;
    }
}
