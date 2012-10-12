/*
 *  gstvaapidownload.c - VA-API video downloader
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
 * SECTION:gstvaapidownload
 * @short_description: A VA to video flow filter
 *
 * vaapidownload converts from VA surfaces to raw YUV pixels.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videocontext.h>

#include "gstvaapidownload.h"
#include "gstvaapipluginutil.h"

#define GST_PLUGIN_NAME "vaapidownload"
#define GST_PLUGIN_DESC "A VA to video flow filter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidownload);
#define GST_CAT_DEFAULT gst_debug_vaapidownload

/*Fixme: format is not only NV12 ?*/

/* Default templates */
static const char gst_vaapidownload_yuv_caps_str[] =
    "video/x-raw, "
    "format=(string)NV12, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

static const char gst_vaapidownload_vaapi_caps_str[] =
    "video/x-raw";

static GstStaticPadTemplate gst_vaapidownload_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidownload_vaapi_caps_str));

static GstStaticPadTemplate gst_vaapidownload_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidownload_yuv_caps_str));

typedef struct _TransformSizeCache TransformSizeCache;
struct _TransformSizeCache {
    GstCaps            *caps;
    guint               size;
};

struct _GstVaapiDownload {
    /*< private >*/
    GstBaseTransform    parent_instance;

    GstVaapiDisplay    *display;
    GstCaps            *allowed_caps;
    TransformSizeCache  transform_size_cache[2];
    GstVaapiImagePool  *images;
    GstVaapiImageFormat image_format;
    guint               image_width;
    guint               image_height;
    unsigned int        images_reset    : 1;
};

struct _GstVaapiDownloadClass {
    /*< private >*/
    GstBaseTransformClass parent_class;
};

static void
gst_video_context_interface_init(GstVideoContextInterface *iface);

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiDownload,
    gst_vaapidownload,
    GST_TYPE_BASE_TRANSFORM,
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

static gboolean
gst_vaapidownload_start(GstBaseTransform *trans);

static gboolean
gst_vaapidownload_stop(GstBaseTransform *trans);

#if 0
static void
gst_vaapidownload_before_transform(GstBaseTransform *trans, GstBuffer *buffer);
#endif

static GstFlowReturn
gst_vaapidownload_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
);

static GstCaps *
gst_vaapidownload_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps,
    GstCaps	     *filter
);

static gboolean
gst_vaapidownload_transform_size(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps,
    gsize             size,
    GstCaps          *othercaps,
    gsize            *othersize
);

static gboolean
gst_vaapidownload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
);

static gboolean
gst_vaapidownload_query(
    GstPad   *pad,
    GstObject *parent,
    GstQuery *query
);

/* GstVideoContext interface */

static void
gst_vaapidownload_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
  GstVaapiDownload *download = GST_VAAPIDOWNLOAD (context);
  gst_vaapi_set_display (type, value, &download->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapidownload_set_video_context;
}

static void
gst_vaapidownload_destroy(GstVaapiDownload *download)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(download->transform_size_cache); i++) {
        TransformSizeCache * const tsc = &download->transform_size_cache[i];
        if (tsc->caps) {
            gst_caps_unref(tsc->caps);
            tsc->caps = NULL;
            tsc->size = 0;
        }
    }

    if (download->allowed_caps) {
        gst_caps_unref(download->allowed_caps);
        download->allowed_caps = NULL;
    }

    g_clear_object(&download->images);
    g_clear_object(&download->display);
}

static void
gst_vaapidownload_finalize(GObject *object)
{
    gst_vaapidownload_destroy(GST_VAAPIDOWNLOAD(object));

    G_OBJECT_CLASS(gst_vaapidownload_parent_class)->finalize(object);
}

static void
gst_vaapidownload_class_init(GstVaapiDownloadClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidownload,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize        = gst_vaapidownload_finalize;
    trans_class->start            = gst_vaapidownload_start;
    trans_class->stop             = gst_vaapidownload_stop;
#if 0
    trans_class->before_transform = gst_vaapidownload_before_transform;
#endif
    trans_class->transform        = gst_vaapidownload_transform;
    trans_class->transform_caps   = gst_vaapidownload_transform_caps;
    trans_class->transform_size   = gst_vaapidownload_transform_size;
    trans_class->set_caps         = gst_vaapidownload_set_caps;

    gst_element_class_set_static_metadata (element_class,
        "VA-API colorspace converter",
        "Filter/Converter/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidownload_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidownload_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
}

static void
gst_vaapidownload_init(GstVaapiDownload *download)
{
    GstPad *sinkpad, *srcpad;

    download->display           = NULL;
    download->allowed_caps      = NULL;
    download->images            = NULL;
    download->images_reset      = FALSE;
    download->image_format      = (GstVaapiImageFormat)0;
    download->image_width       = 0;
    download->image_height      = 0;

    /* Override buffer allocator on sink pad */
    sinkpad = gst_element_get_static_pad(GST_ELEMENT(download), "sink");
    gst_pad_set_query_function(sinkpad, gst_vaapidownload_query);
    gst_object_unref(sinkpad);

    /* Override query on src pad */
    srcpad = gst_element_get_static_pad(GST_ELEMENT(download), "src");
    gst_pad_set_query_function(srcpad, gst_vaapidownload_query);
    gst_object_unref(srcpad);
}

static inline gboolean
gst_vaapidownload_ensure_display(GstVaapiDownload *download)
{
    return gst_vaapi_ensure_display(download, GST_VAAPI_DISPLAY_TYPE_ANY,
        &download->display);
}

static gboolean
gst_vaapidownload_start(GstBaseTransform *trans)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    if (!gst_vaapidownload_ensure_display(download))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapidownload_stop(GstBaseTransform *trans)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    g_clear_object(&download->display);

    return TRUE;
}

static GstVaapiImageFormat
get_surface_format(GstVaapiSurface *surface)
{
    GstVaapiImage *image;
    GstVaapiImageFormat format = GST_VAAPI_IMAGE_NV12;

    /* XXX: NV12 is assumed by default */
    image = gst_vaapi_surface_derive_image(surface);
    if (image) {
        format = gst_vaapi_image_get_format(image);
        g_object_unref(image);
    }
    return format;
}

#if 0
static gboolean
gst_vaapidownload_update_src_caps(GstVaapiDownload *download, GstBuffer *buffer)
{
    GstVaapiSurface *surface;
    GstVaapiImageFormat format;
    GstPad *srcpad;
    GstCaps *in_caps, *out_caps;
    GstMapInfo info;
  
    gst_buffer_map (buf, &map_info, GST_MAP_READ);
    surface = map_info.data;
    if (!surface){
        GST_DEBUG_OBJECT (sink, "Failed to retrieve the VA surface from buffer");
        return FALSE;
    }

    format = get_surface_format(surface);
    if (format == download->image_format)
        return TRUE;

    in_caps = GST_BUFFER_CAPS(buffer);
    if (!in_caps) {
        GST_WARNING("failed to retrieve caps from buffer");
        return FALSE;
    }

    out_caps = gst_vaapi_image_format_get_caps(format);
    if (!out_caps) {
        GST_WARNING("failed to create caps from format %" GST_FOURCC_FORMAT,
                    GST_FOURCC_ARGS(format));
        return FALSE;
    }

    if (!gst_vaapi_append_surface_caps(out_caps, in_caps)) {
        gst_caps_unref(out_caps);
        return FALSE;
    }

    /* Try to renegotiate downstream caps */
    srcpad = gst_element_get_static_pad(GST_ELEMENT(download), "src");
    gst_pad_set_caps(srcpad, out_caps);
    gst_object_unref(srcpad);

    gst_vaapidownload_set_caps(GST_BASE_TRANSFORM(download), in_caps, out_caps);
    gst_caps_replace(&download->allowed_caps, out_caps);
    gst_caps_unref(out_caps);
    return TRUE;
}

static void
gst_vaapidownload_before_transform(GstBaseTransform *trans, GstBuffer *buffer)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    gst_vaapidownload_update_src_caps(download, buffer);
}
#endif
static GstFlowReturn
gst_vaapidownload_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);
    GstVaapiSurface *surface;
    GstVaapiImage *image = NULL;
    gboolean success;
    GstBuffer *buf;
    GstMapInfo in_info;
    GstMapInfo out_info;
    GstMapInfo info;
    GstVaapiSurfaceMeta *meta;
 
    meta = gst_buffer_get_vaapi_surface_meta (inbuf); 
    surface = (GstVaapiSurface *)meta->surface;
    if (!surface){
        GST_DEBUG_OBJECT (download, "Failed to retrieve the VA surface from buffer");
        return GST_FLOW_EOS;
    }

    if (gst_buffer_pool_acquire_buffer((GstBufferPool *)download->images, &buf, NULL) != GST_FLOW_OK){
        GST_ERROR("Failed to acquire buffer");
        buf = NULL;
    }
    if (buf) {
        gst_buffer_map(buf, &info, GST_MAP_WRITE);
        image = (GstVaapiImage *)info.data;
    }
    if (!image) { 
        GST_DEBUG_OBJECT (download, "Failed to retrieve the VA image from buffer");
        return GST_FLOW_EOS;
    }

    if (!gst_vaapi_surface_get_image(surface, image))
        goto error_get_image;

    success = gst_vaapi_image_get_buffer(image, outbuf, NULL);
    gst_buffer_pool_release_buffer((GstBufferPool *)download->images, buf);
    if (!success)
        goto error_get_buffer;
    return GST_FLOW_OK;

error_get_image:
    {
        GST_WARNING("failed to download %" GST_FOURCC_FORMAT " image "
                    "from surface 0x%08x",
                    GST_FOURCC_ARGS(gst_vaapi_image_get_format(image)),
                    gst_vaapi_surface_get_id(surface));
        gst_buffer_pool_release_buffer((GstBufferPool *)download->images, buf);
        return GST_FLOW_EOS;
    }

error_get_buffer:
    {
        GST_WARNING("failed to transfer image to output video buffer");
        return GST_FLOW_EOS;
    }
}

static GstCaps *
gst_vaapidownload_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps,
    GstCaps	     *filter
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);
    GstPad *srcpad;
    GstCaps *allowed_caps, *inter_caps, *out_caps = NULL;
    GstStructure *structure;

    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    structure = gst_caps_get_structure(caps, 0);

    if (direction == GST_PAD_SINK) {
        if (!gst_structure_has_name(structure, "video/x-raw"))
            return NULL;
        if (!gst_vaapidownload_ensure_display(download))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapidownload_yuv_caps_str);

        /* Build up allowed caps */
        /* XXX: we don't know the decoded surface format yet so we
           expose whatever VA images we support */
        if (download->allowed_caps)
            allowed_caps = gst_caps_ref(download->allowed_caps);
        else {
            allowed_caps = gst_vaapi_display_get_image_caps(download->display);
            if (!allowed_caps)
                return NULL;
        }
        inter_caps = gst_caps_intersect(out_caps, allowed_caps);
        gst_caps_unref(allowed_caps);
        gst_caps_unref(out_caps);
        out_caps = inter_caps;

        /* Intersect with allowed caps from the peer, if any */
        srcpad = gst_element_get_static_pad(GST_ELEMENT(download), "src");
        allowed_caps = gst_pad_peer_query_caps(srcpad, NULL);
        if (allowed_caps) {
            inter_caps = gst_caps_intersect(out_caps, allowed_caps);
            gst_caps_unref(allowed_caps);
            gst_caps_unref(out_caps);
            out_caps = inter_caps;
        }
    }
    else {
        if (!gst_structure_has_name(structure, "video/x-raw"))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapidownload_vaapi_caps_str);

        structure = gst_caps_get_structure(out_caps, 0);
        gst_structure_set(
            structure,
            "type", G_TYPE_STRING, "vaapi",
            "opengl", G_TYPE_BOOLEAN, USE_GLX,
            NULL
        );
    }

    if (!gst_vaapi_append_surface_caps(out_caps, caps)) {
        gst_caps_unref(out_caps);
        return NULL;
    }
    return out_caps;
}

static gboolean
gst_vaapidownload_ensure_image_pool(GstVaapiDownload *download, GstCaps *caps)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVaapiImageFormat format;
    gint width, height;
    GstStructure *config;

    format = gst_vaapi_image_format_from_caps(caps);
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (format != download->image_format ||
        width  != download->image_width  ||
        height != download->image_height) {
        download->image_format = format;
        download->image_width  = width;
        download->image_height = height;
        g_clear_object(&download->images);
        download->images = (GstVaapiImagePool *)gst_vaapi_image_pool_new(download->display, caps);
        if (!download->images)
            return FALSE;
	/*Fixme: remove hard-coded min and max values*/	
	config = gst_buffer_pool_get_config ((GstBufferPool *)download->images);
        gst_buffer_pool_config_set_params (config, caps, width*height, 6, 24);

    	gst_buffer_pool_set_config ((GstBufferPool *)download->images, config);
	gst_buffer_pool_set_active ((GstBufferPool *)download->images, TRUE);

        download->images_reset = TRUE;
    }
    return TRUE;
}

static inline gboolean
gst_vaapidownload_negotiate_buffers(
    GstVaapiDownload  *download,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    if (!gst_vaapidownload_ensure_image_pool(download, outcaps))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapidownload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    if (!gst_vaapidownload_negotiate_buffers(download, incaps, outcaps))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapidownload_transform_size(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps,
    gsize             size,
    GstCaps          *othercaps,
    gsize            *othersize
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);
    GstStructure * const structure = gst_caps_get_structure(othercaps, 0);
    GstVideoFormat format;
    GstVideoInfo info;
    gint width, height;
    guint i;

    /* Lookup in cache */
    for (i = 0; i < G_N_ELEMENTS(download->transform_size_cache); i++) {
        TransformSizeCache * const tsc = &download->transform_size_cache[i];
        if (tsc->caps && tsc->caps == othercaps) {
            *othersize = tsc->size;
            return TRUE;
        }
    }

    /* Compute requested buffer size */
    if (gst_structure_has_name(structure, "video/x-raw"))
        *othersize = 0;
    else {
        if (!gst_video_info_from_caps(&info, othercaps))
	    return FALSE;

 	width      = info.width;
	height     = info.height; 
	*othersize = info.size;

        /*if (!gst_video_format_parse_caps(othercaps, &format, &width, &height))
            return FALSE;
        *othersize = gst_video_format_get_size(format, width, height);*/
    }

    /* Update cache */
    for (i = 0; i < G_N_ELEMENTS(download->transform_size_cache); i++) {
        TransformSizeCache * const tsc = &download->transform_size_cache[i];
        if (!tsc->caps) {
            gst_caps_replace(&tsc->caps, othercaps);
            tsc->size = *othersize;
        }
    }
    return TRUE;
}

static gboolean
gst_vaapidownload_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(parent);
    gboolean res;

    GST_DEBUG("sharing display %p", download->display);

    if (gst_vaapi_reply_to_query(query, download->display))
        res = TRUE;
    else
        res = gst_pad_query_default(pad, parent, query);

    return res;
}
