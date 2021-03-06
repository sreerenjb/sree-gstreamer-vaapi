/*
 *  gstvaapidecoder_mpeg2.h - MPEG-2 decoder
 *
 *  Copyright (C) 2011 Intel Corporation
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

#ifndef GST_VAAPI_DECODER_MPEG2_H
#define GST_VAAPI_DECODER_MPEG2_H

#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DECODER_MPEG2 \
    (gst_vaapi_decoder_mpeg2_get_type())

#define GST_VAAPI_DECODER_MPEG2(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DECODER_MPEG2,   \
                                GstVaapiDecoderMpeg2))

#define GST_VAAPI_DECODER_MPEG2_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DECODER_MPEG2,      \
                             GstVaapiDecoderMpeg2Class))

#define GST_VAAPI_IS_DECODER_MPEG2(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DECODER_MPEG2))

#define GST_VAAPI_IS_DECODER_MPEG2_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DECODER_MPEG2))

#define GST_VAAPI_DECODER_MPEG2_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DECODER_MPEG2,    \
                               GstVaapiDecoderMpeg2Class))

typedef struct _GstVaapiDecoderMpeg2            GstVaapiDecoderMpeg2;
typedef struct _GstVaapiDecoderMpeg2Private     GstVaapiDecoderMpeg2Private;
typedef struct _GstVaapiDecoderMpeg2Class       GstVaapiDecoderMpeg2Class;

/**
 * GstVaapiDecoderMpeg2:
 *
 * A decoder based on Mpeg2.
 */
struct _GstVaapiDecoderMpeg2 {
    /*< private >*/
    GstVaapiDecoder parent_instance;

    GstVaapiDecoderMpeg2Private *priv;
};

/**
 * GstVaapiDecoderMpeg2Class:
 *
 * A decoder class based on Mpeg2.
 */
struct _GstVaapiDecoderMpeg2Class {
    /*< private >*/
    GstVaapiDecoderClass parent_class;
};

GType
gst_vaapi_decoder_mpeg2_get_type(void) G_GNUC_CONST;

GstVaapiDecoder *
gst_vaapi_decoder_mpeg2_new(GstVaapiDisplay *display, GstCaps *caps);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_MPEG2_H */
