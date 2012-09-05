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
typedef struct _GstVaapiHuffmanTable            GstVaapiHuffmanTable;

/* ------------------------------------------------------------------------- */
/* --- Base Codec Object                                                 --- */
/* ------------------------------------------------------------------------- */

/* XXX: remove when a common base class for decoder and encoder is available */
#define GST_VAAPI_CODEC_BASE(obj) \
    ((GstVaapiCodecBase *)(obj))

#define GST_VAAPI_TYPE_CODEC_OBJECT             (gst_vaapi_codec_object_get_type())
//#define GST_VAAPI_IS_CODEC_OBJECT_TYPE(obj)     (GST_IS_MINI_OBJECT_TYPE(obj))
#define GST_VAAPI_CODEC_OBJECT_CAST(obj)        ((GstVaapiCodecObject *)(obj))
#define GST_VAAPI_CODEC_OBJECT(obj)             (GST_VAAPI_CODEC_OBJECT_CAST(obj))

enum {
    GST_VAAPI_CODEC_OBJECT_FLAG_CONSTRUCTED = (GST_MINI_OBJECT_FLAG_LAST << 0),
    GST_VAAPI_CODEC_OBJECT_FLAG_LAST        = (GST_MINI_OBJECT_FLAG_LAST << 1)
};

typedef struct {
    GstVaapiCodecObject        *obj;
    GstVaapiCodecBase          *codec;
    gconstpointer               param;
    guint                       param_size;
    gconstpointer               data;
    guint                       data_size;
    guint                       flags;
    GstVideoCodecFrame          *frame;
} GstVaapiCodecObjectConstructorArgs;

/**
 * GstVaapiCodecObject:
 *
 * A #GstMiniObject holding the base codec object data
 */
struct _GstVaapiCodecObject {
    /*< private >*/
    GstMiniObject               parent_instance;
    GstVaapiCodecBase          *codec;
  
    gboolean (*initialize_ob) (GstVaapiCodecObject *obj);    
    gboolean (*construct_obj) (GstVaapiCodecObject *obj, const GstVaapiCodecObjectConstructorArgs *args); 
};

G_GNUC_INTERNAL
GType
gst_vaapi_codec_object_get_type(void) G_GNUC_CONST;

/*
G_GNUC_INTERNAL
GstVaapiCodecObject *
gst_vaapi_codec_object_new(
    GType              type,
    GstVaapiCodecBase *codec,
    gconstpointer      param,
    guint              param_size,
    gconstpointer      data,
    guint              data_size    
);
*/

G_GNUC_INTERNAL
GstVaapiCodecObject *
gst_vaapi_codec_object_finish(
    GstVaapiCodecObject  *va_obj,
    GstVaapiCodecBase    *codec,
    gconstpointer        param,
    guint                param_size,
    gconstpointer        data,
    guint                data_size,
    guint		 flags
);

G_GNUC_INTERNAL
gboolean
gst_vaapi_codec_object_construct(
    GstVaapiCodecObject                      *obj,
    const GstVaapiCodecObjectConstructorArgs *args
);

/* ------------------------------------------------------------------------- */
/* --- Inverse Quantization Matrices                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_IQ_MATRIX        (gst_vaapi_iq_matrix_get_type())
#define GST_VAAPI_IS_IQ_MATRIX(obj)     (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_IQ_MATRIX))
#define GST_VAAPI_IQ_MATRIX_CAST(obj)   ((GstVaapiIqMatrix *)(obj))
#define GST_VAAPI_IQ_MATRIX(obj)        (GST_VAAPI_IQ_MATRIX_CAST(obj)

/**
 * GstVaapiIqMatrix:
 *
 * A #GstVaapiCodecObject holding an inverse quantization matrix parameter.
 */
struct _GstVaapiIqMatrix {
    /*< private >*/
    GstVaapiCodecObject         parent_instance;
    GstVaapiCodecObjectConstructorArgs args;
    VABufferID                  param_id;

    /*< public >*/
    gpointer                    param;
};

G_GNUC_INTERNAL
GType
gst_vaapi_iq_matrix_get_type(void)G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiIqMatrix *
gst_vaapi_iq_matrix_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size
);

/* ------------------------------------------------------------------------- */
/* --- VC-1 Bit Planes                                                   --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_BITPLANE       (gst_vaapi_bitplane_get_type())
#define GST_VAAPI_BITPLANE_CAST(obj)  ((GstVaapiBitPlane *)(obj))
#define GST_VAAPI_BITPLANE(obj)       (GST_VAAPI_BITPLANE_CAST(obj))    
#define GST_VAAPI_IS_BITPLANE(obj)    (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_BITPLANE))

/**
 * GstVaapiBitPlane:
 *
 * A #GstVaapiCodecObject holding a VC-1 bit plane parameter.
 */
struct _GstVaapiBitPlane {
    /*< private >*/
    GstVaapiCodecObject         parent_instance;
    GstVaapiCodecObjectConstructorArgs args;
    VABufferID                  data_id;

    /*< public >*/
    guint8                     *data;
};

G_GNUC_INTERNAL
GType
gst_vaapi_bitplane_get_type(void)G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiBitPlane *
gst_vaapi_bitplane_new(GstVaapiDecoder *decoder, guint8 *data, guint data_size);

/* ------------------------------------------------------------------------- */
/* --- JPEG Huffman Tables                                               --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_HUFFMAN_TABLE       (gst_vaapi_huffman_table_get_type())
#define GST_VAAPI_HUFFMAN_TABLE_CAST(obj)  ((GstVaapiHuffmanTable *)(obj))
#define GST_VAAPI_HUFFMAN_TABLE(obj)       (GST_VAAPI_HUFFMAN_TABLE_CAST(obj))    
#define GST_VAAPI_IS_HUFFMAN_TABLE(obj)    (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_HUFFMAN_TABLE))


/**
 * GstVaapiHuffmanTable:
 *
 * A #GstVaapiCodecObject holding huffman table.
 */
struct _GstVaapiHuffmanTable {
    /*< private >*/
    GstVaapiCodecObject         parent_instance;
    GstVaapiCodecObjectConstructorArgs args;
    VABufferID                  param_id;

    /*< public >*/
    gpointer                    param;
};

G_GNUC_INTERNAL
GType
gst_vaapi_huffman_table_get_type(void)G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiHuffmanTable *
gst_vaapi_huffman_table_new(
    GstVaapiDecoder *decoder,
    guint8          *data,
    guint            data_size
);


/* ------------------------------------------------------------------------- */
/* --- Helpers to create codec-dependent objects                         --- */
/* ------------------------------------------------------------------------- */
#define GST_VAAPI_CODEC_DEFINE_TYPE(type, prefix)            		\
GST_DEFINE_MINI_OBJECT_TYPE(type, prefix)                               \
                                                                        \
void                                                             \
prefix##_destroy(type *);                                               \
                                                                        \
gboolean                                                         \
prefix##_create(                                                        \
    type *,                                                             \
    const GstVaapiCodecObjectConstructorArgs *args                      \
);                                                                      \
                                                                        \
static void                                                             \
prefix##_free(GstMiniObject *object)                                    \
{									\
    prefix##_destroy((type *)object);                                   \
}     									\
									\
static gboolean                                                         \
prefix##_construct(                                                     \
    GstVaapiCodecObject                      *object,                   \
    const GstVaapiCodecObjectConstructorArgs *args                      \
)                                                                       \
{   									\
    object->codec = args->codec;                                        \
    return prefix##_create((type *)object, args);                       \
}                                                                       \
                                                                        \
static void                                                             \
prefix##_initialize(type *obj)                                			    \
{										    \
    GstVaapiCodecObject *codec_obj;                      			    \
    gst_mini_object_init (GST_MINI_OBJECT_CAST (obj), 0, (GType)prefix##_get_type,  \
        (GstMiniObjectCopyFunction) NULL,					    \
        (GstMiniObjectDisposeFunction) NULL,			   	            \
        (GstMiniObjectFreeFunction) prefix##_free);			            \
    codec_obj	= GST_VAAPI_CODEC_OBJECT_CAST(obj);		                    \
    codec_obj->codec = NULL;				                            \
    codec_obj->construct_obj = prefix##_construct;                                  \
}
										    
#define GST_VAAPI_IQ_MATRIX_NEW(codec, decoder)                         \
    gst_vaapi_iq_matrix_new(GST_VAAPI_DECODER_CAST(decoder),            \
                            NULL, sizeof(VAIQMatrixBuffer##codec))

#define GST_VAAPI_BITPLANE_NEW(decoder, size) \
    gst_vaapi_bitplane_new(GST_VAAPI_DECODER_CAST(decoder), NULL, size)

#define GST_VAAPI_HUFFMAN_TABLE_NEW(codec, decoder)                     \
      gst_vaapi_huffman_table_new(GST_VAAPI_DECODER_CAST(decoder),      \
                            NULL, sizeof(VAHuffmanTableBuffer##codec))

G_END_DECLS

#endif /* GST_VAAPI_CODEC_OBJECTS_H */
