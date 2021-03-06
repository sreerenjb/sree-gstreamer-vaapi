/*
 *  gstvaapidecoder_objects.h - VA decoder objects
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

#ifndef GST_VAAPI_DECODER_OBJECTS_H
#define GST_VAAPI_DECODER_OBJECTS_H

#include <gst/vaapi/gstvaapicodec_objects.h>

G_BEGIN_DECLS

typedef struct _GstVaapiPicture         GstVaapiPicture;
typedef struct _GstVaapiPictureClass    GstVaapiPictureClass;
typedef struct _GstVaapiSlice           GstVaapiSlice;
typedef struct _GstVaapiSliceClass      GstVaapiSliceClass;

/* ------------------------------------------------------------------------- */
/* --- Pictures                                                          --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_PICTURE \
    (gst_vaapi_picture_get_type())

#define GST_VAAPI_PICTURE_CAST(obj) \
    ((GstVaapiPicture *)(obj))

#define GST_VAAPI_PICTURE(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_PICTURE, \
                                GstVaapiPicture))

#define GST_VAAPI_PICTURE_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_PICTURE,    \
                             GstVaapiPictureClass))

#define GST_VAAPI_IS_PICTURE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_PICTURE))

#define GST_VAAPI_IS_PICTURE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_PICTURE))

#define GST_VAAPI_PICTURE_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_PICTURE,  \
                               GstVaapiPictureClass))

typedef enum {
    GST_VAAPI_PICTURE_TYPE_NONE = 0,        // Undefined
    GST_VAAPI_PICTURE_TYPE_I,               // Intra
    GST_VAAPI_PICTURE_TYPE_P,               // Predicted
    GST_VAAPI_PICTURE_TYPE_B,               // Bi-directional predicted
    GST_VAAPI_PICTURE_TYPE_S,               // S(GMC)-VOP (MPEG-4)
    GST_VAAPI_PICTURE_TYPE_SI,              // Switching Intra
    GST_VAAPI_PICTURE_TYPE_SP,              // Switching Predicted
    GST_VAAPI_PICTURE_TYPE_BI,              // BI type (VC-1)
} GstVaapiPictureType;

/**
 * Picture flags:
 * @GST_VAAPI_PICTURE_FLAG_SKIPPED: skipped frame
 * @GST_VAAPI_PICTURE_FLAG_REFERENCE: reference frame
 * @GST_VAAPI_PICTURE_FLAG_OUTPUT: frame was output
 * @GST_VAAPI_PICTURE_FLAG_INTERLACED: interlaced frame
 * @GST_VAAPI_PICTURE_FLAG_FF: first-field
 * @GST_VAAPI_PICTURE_FLAG_TFF: top-field-first
 * @GST_VAAPI_PICTURE_FLAG_LAST: first flag that can be used by subclasses
 *
 * Enum values used for #GstVaapiPicture flags.
 */
enum {
    GST_VAAPI_PICTURE_FLAG_SKIPPED    = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 0),
    GST_VAAPI_PICTURE_FLAG_REFERENCE  = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 1),
    GST_VAAPI_PICTURE_FLAG_OUTPUT     = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 2),
    GST_VAAPI_PICTURE_FLAG_INTERLACED = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 3),
    GST_VAAPI_PICTURE_FLAG_FF         = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 4),
    GST_VAAPI_PICTURE_FLAG_TFF        = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 5),
    GST_VAAPI_PICTURE_FLAG_LAST       = (GST_VAAPI_CODEC_OBJECT_FLAG_LAST << 6),
};

#define GST_VAAPI_PICTURE_FLAGS         GST_MINI_OBJECT_FLAGS
#define GST_VAAPI_PICTURE_FLAG_IS_SET   GST_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_PICTURE_FLAG_SET      GST_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_PICTURE_FLAG_UNSET    GST_MINI_OBJECT_FLAG_UNSET

#define GST_VAAPI_PICTURE_IS_SKIPPED(picture) \
    GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_SKIPPED)

#define GST_VAAPI_PICTURE_IS_REFERENCE(picture) \
    GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE)

#define GST_VAAPI_PICTURE_IS_OUTPUT(picture) \
    GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_OUTPUT)

#define GST_VAAPI_PICTURE_IS_INTERLACED(picture) \
    GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_INTERLACED)

#define GST_VAAPI_PICTURE_IS_FIRST_FIELD(picture) \
    GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_FF)

#define GST_VAAPI_PICTURE_IS_TFF(picture) \
    GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_TFF)

#define GST_VAAPI_PICTURE_IS_FRAME(picture) \
    (GST_VAAPI_PICTURE(picture)->structure == GST_VAAPI_PICTURE_STRUCTURE_FRAME)

#define GST_VAAPI_PICTURE_IS_COMPLETE(picture)          \
    (GST_VAAPI_PICTURE_IS_FRAME(picture) ||             \
     !GST_VAAPI_PICTURE_IS_FIRST_FIELD(picture))

/**
 * GstVaapiPicture:
 *
 * A #GstVaapiCodecObject holding a picture parameter.
 */
struct _GstVaapiPicture {
    /*< private >*/
    GstVaapiCodecObject         parent_instance;
    GstVaapiSurface            *surface;
    GstVaapiSurfaceProxy       *proxy;
    VABufferID                  param_id;
    guint                       param_size;

    /*< public >*/
    GstVaapiPictureType         type;
    VASurfaceID                 surface_id;
    gpointer                    param;
    GPtrArray                  *slices;
    GstVaapiIqMatrix           *iq_matrix;
    GstVaapiHuffmanTable       *huf_table;
    GstVaapiBitPlane           *bitplane;
    GstClockTime                pts;
    gint32                      poc;
    guint                       structure;
};

/**
 * GstVaapiPictureClass:
 *
 * The #GstVaapiPicture base class.
 */
struct _GstVaapiPictureClass {
    /*< private >*/
    GstVaapiCodecObjectClass    parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_picture_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiPicture *
gst_vaapi_picture_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size
);

G_GNUC_INTERNAL
GstVaapiPicture *
gst_vaapi_picture_new_field(GstVaapiPicture *picture);

G_GNUC_INTERNAL
void
gst_vaapi_picture_add_slice(GstVaapiPicture *picture, GstVaapiSlice *slice);

G_GNUC_INTERNAL
gboolean
gst_vaapi_picture_decode(GstVaapiPicture *picture);

G_GNUC_INTERNAL
gboolean
gst_vaapi_picture_output(GstVaapiPicture *picture);

static inline gpointer
gst_vaapi_picture_ref(gpointer ptr)
{
    return gst_mini_object_ref(GST_MINI_OBJECT(ptr));
}

static inline void
gst_vaapi_picture_unref(gpointer ptr)
{
    gst_mini_object_unref(GST_MINI_OBJECT(ptr));
}

#define gst_vaapi_picture_replace(old_picture_p, new_picture)   \
    gst_mini_object_replace((GstMiniObject **)(old_picture_p),  \
                            (GstMiniObject *)(new_picture))

/* ------------------------------------------------------------------------- */
/* --- Slices                                                            --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_SLICE \
    (gst_vaapi_slice_get_type())

#define GST_VAAPI_SLICE_CAST(obj) \
    ((GstVaapiSlice *)(obj))

#define GST_VAAPI_SLICE(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_SLICE,   \
                                GstVaapiSlice))

#define GST_VAAPI_SLICE_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_SLICE,      \
                             GstVaapiSliceClass))

#define GST_VAAPI_IS_SLICE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SLICE))

#define GST_VAAPI_IS_SLICE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SLICE))

#define GST_VAAPI_SLICE_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_SLICE,    \
                               GstVaapiSliceClass))

/**
 * GstVaapiSlice:
 *
 * A #GstVaapiCodecObject holding a slice parameter.
 */
struct _GstVaapiSlice {
    /*< private >*/
    GstVaapiCodecObject         parent_instance;

    /*< public >*/
    VABufferID                  param_id;
    VABufferID                  data_id;
    gpointer                    param;
};

/**
 * GstVaapiSliceClass:
 *
 * The #GstVaapiSlice base class.
 */
struct _GstVaapiSliceClass {
    /*< private >*/
    GstVaapiCodecObjectClass    parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_slice_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiSlice *
gst_vaapi_slice_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size,
    const guchar    *data,
    guint            data_size
);

/* ------------------------------------------------------------------------- */
/* --- Helpers to create codec-dependent objects                         --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_PICTURE_NEW(codec, decoder)                           \
    gst_vaapi_picture_new(GST_VAAPI_DECODER_CAST(decoder),              \
                          NULL, sizeof(VAPictureParameterBuffer##codec))

#define GST_VAAPI_SLICE_NEW(codec, decoder, buf, buf_size)              \
    gst_vaapi_slice_new(GST_VAAPI_DECODER_CAST(decoder),                \
                        NULL, sizeof(VASliceParameterBuffer##codec),    \
                        buf, buf_size)

G_END_DECLS

#endif /* GST_VAAPI_DECODER_OBJECTS_H */
