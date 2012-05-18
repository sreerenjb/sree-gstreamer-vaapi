/*
 *  gstvaapicodec_objects.h - VA codec objects abstraction
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

#ifndef GST_VAAPI_CODEC_COMMON_H
#define GST_VAAPI_CODEC_COMMON_H

#include <gst/gstminiobject.h>
#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

typedef gpointer                                GstVaapiCodecBase;
typedef struct _GstVaapiCodecObject             GstVaapiCodecObject;
typedef struct _GstVaapiIqMatrix                GstVaapiIqMatrix;
typedef struct _GstVaapiBitPlane                GstVaapiBitPlane;

/* ------------------------------------------------------------------------- */
/* --- Base Codec Object                                                 --- */
/* ------------------------------------------------------------------------- */

/* XXX: remove when a common base class for decoder and encoder is available */
#define GST_VAAPI_CODEC_BASE(obj) \
    ((GstVaapiCodecBase *)(obj))

enum {
    GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED = (GST_MINI_OBJECT_FLAG_LAST << 0),
    GST_VAAPI_CODEC_OBJECT_FLAG_LAST        = (GST_MINI_OBJECT_FLAG_LAST << 1)
};

typedef struct {
    GstVaapiCodecBase          *codec;
    gconstpointer               param;
    guint                       param_size;
    gconstpointer               data;
    guint                       data_size;
    guint                       flags;
} GstVaapiCodecObjectConstructorArgs;


/* ------------------------------------------------------------------------- */
/* --- Inverse Quantization Matrices                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_IQ_MATRIX        (gst_vaapi_iq_matrix_get_type())
#define GST_VAAPI_IS_IQ_MATRIX(obj)     (GST_IS_MINI_OBJECT_TYPE (obj, GST_VAAPI_TYPE_IQ_MATRIX)
#define GST_VAAPI_IQ_MATRIX_CAST(obj)   ((GstVaapiIqMatrix *)(obj))
#define GST_VAAPI_IQ_MATRIX(obj)        (GST_VAAPI_IQ_MATRIX_CAST(obj)

/**
 * GstVaapiIqMatrix:
 *
 * A #GstVaapiCodecObject holding an inverse quantization matrix parameter.
 */
struct _GstVaapiIqMatrix {
    /*< private >*/
    GstMiniObject         	parent_instance;
    GstVaapiCodecObjectConstructorArgs args;
    VABufferID                  param_id;

    /*< public >*/
    gpointer                    param;
};

GType
gst_vaapi_iq_matrix_get_type(void)
    attribute_hidden;

GstVaapiIqMatrix *
gst_vaapi_iq_matrix_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size
) attribute_hidden;

/* ------------------------------------------------------------------------- */
/* --- VC-1 Bit Planes                                                   --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_BITPLANE       (gst_vaapi_bitplane_get_type())
#define GST_VAAPI_BITPLANE_CAST(obj)  ((GstVaapiBitPlane *)(obj))
#define GST_VAAPI_BITPLANE(obj)       (GST_VAAPI_BITPLANE_CAST(obj))    
#define GST_VAAPI_IS_BITPLANE(obj)    (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_BITPLANE)


/**
 * GstVaapiBitPlane:
 *
 * A #GstVaapiCodecObject holding a VC-1 bit plane parameter.
 */
struct _GstVaapiBitPlane {
    /*< private >*/
    GstMiniObject               parent_instance;
    GstVaapiCodecObjectConstructorArgs args;
    VABufferID                  data_id;

    /*< public >*/
    guint8                     *data;
};


GType
gst_vaapi_bitplane_get_type(void)
    attribute_hidden;

GstVaapiBitPlane *
gst_vaapi_bitplane_new(GstVaapiDecoder *decoder, guint8 *data, guint data_size)
    attribute_hidden;


/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_HUFFMAN_TABLE       (gst_vaapi_huffman_table_get_type())
#define GST_VAAPI_HUFFMAN_TABLE_CAST(obj)  ((GstVaapiHuffmanTable *)(obj))
#define GST_VAAPI_HUFFMAN_TABLE(obj)       (GST_VAAPI_HUFFMAN_TABLE_CAST(obj))    
#define GST_VAAPI_IS_HUFFMAN_TABLE(obj)    (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_HUFFMAN_TABLE)

/**
 * GstVaapiHuffmanTable:
 *
 * A #GstVaapiCodecObject holding huffman table.
 */
struct _GstVaapiHuffmanTable {
    /*< private >*/
    GstMiniObject               parent_instance;
    GstVaapiCodecObjectConstructorArgs args;

    VABufferID                  param_id;

    /*< public >*/
    gpointer                    param;
};

GType
gst_vaapi_huffman_table_get_type(void)
    attribute_hidden;

GstVaapiHuffmanTable *
gst_vaapi_huffman_table_new(
    GstVaapiDecoder *decoder,
    guint8          *data,
    guint            data_size
) attribute_hidden;


/* ------------------------------------------------------------------------- */
/* --- Helpers to create codec-dependent objects                         --- */
/* ------------------------------------------------------------------------- */
									  
/*                                                                        \
static void                                                             \
prefix##_destroy(type *);                                               \
                                                                        \
static gboolean                                                         \
prefix##_create(                                                        \
    type *,                                                             \
    const GstVaapiCodecObjectConstructorArgs *args                      \
);                                                                      \
*/
/*
#define GST_VAAPI_CODEC_DEFINE_TYPE(type, prefix)            		\
GST_DEFINE_MINI_OBJECT_TYPE (type, prefix);				\
                                                                        \
*/
#define GST_VAAPI_IQ_MATRIX_NEW(codec, decoder)                         \
    gst_vaapi_iq_matrix_new(GST_VAAPI_DECODER_CAST(decoder),            \
                            NULL, sizeof(VAIQMatrixBuffer##codec))

#define GST_VAAPI_BITPLANE_NEW(decoder, size) \
    gst_vaapi_bitplane_new(GST_VAAPI_DECODER_CAST(decoder), NULL, size)

#define GST_VAAPI_HUFFMAN_TABLE_NEW(codec, decoder)              \
      gst_vaapi_huffman_table_new(GST_VAAPI_DECODER_CAST(decoder),    \
                            NULL, sizeof(VAHuffmanTableBuffer##codec))

G_END_DECLS

#endif /* GST_VAAPI_CODEC_OBJECTS_H */
