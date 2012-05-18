/*
 *  gstvaapicodec_objects.c - VA codec objects abstraction
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

#include "sysdeps.h"
#include <string.h>
#include <gst/vaapi/gstvaapicontext.h>
#include "gstvaapicodec_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* ------------------------------------------------------------------------- */
/* --- Base Codec Object                                                 --- */
/* ------------------------------------------------------------------------- */

#define GET_DECODER(obj)    GST_VAAPI_DECODER_CAST((obj)->args.codec)
#define GET_CONTEXT(obj)    GET_DECODER(obj)->priv->context
#define GET_VA_DISPLAY(obj) GET_DECODER(obj)->priv->va_display
#define GET_VA_CONTEXT(obj) GET_DECODER(obj)->priv->va_context

/* ------------------------------------------------------------------------- */
/* --- Inverse Quantization Matrices                                     --- */
/* ------------------------------------------------------------------------- */
GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiIqMatrix, gst_vaapi_iq_matrix);
/*GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiIqMatrix,
                            gst_vaapi_iq_matrix)*/

static void
gst_vaapi_iq_matrix_destroy(GstVaapiIqMatrix *iq_matrix)
{
    vaapi_destroy_buffer(GET_VA_DISPLAY(iq_matrix), &iq_matrix->param_id);
    iq_matrix->param = NULL;
}

static gboolean
gst_vaapi_iq_matrix_create(
    GstVaapiIqMatrix                         *iq_matrix,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return vaapi_create_buffer(GET_VA_DISPLAY(iq_matrix),
                               GET_VA_CONTEXT(iq_matrix),
                               VAIQMatrixBufferType,
                               args->param_size,
                               args->param,
                               &iq_matrix->param_id,
                               &iq_matrix->param);
}

GstVaapiIqMatrix *
gst_vaapi_iq_matrix_new(
    GstVaapiDecoder *decoder,
    gconstpointer    param,
    guint            param_size
)
{
    GstVaapiIqMatrix  *obj;
    GstVaapiCodecObjectConstructorArgs args;
    
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    obj = g_slice_new0(GstVaapiIqMatrix);
    if (!obj)
        return NULL;

    gst_mini_object_init (GST_MINI_OBJECT_CAST (obj), GST_VAAPI_TYPE_IQ_MATRIX, sizeof(GstVaapiIqMatrix));

    obj->parent_instance.free =
      (GstMiniObjectFreeFunction) gst_vaapi_iq_matrix_destroy;

    obj->args.codec      = GST_VAAPI_CODEC_BASE(decoder);
    obj->args.param      = param;
    obj->args.param_size = param_size;
    obj->args.data       = NULL;
    obj->args.data_size  = 0;
    obj->args.flags      = 0;

    if (!gst_vaapi_iq_matrix_create (obj, &(obj->args)))
	return NULL;

    return GST_VAAPI_IQ_MATRIX_CAST(obj);
}

/* ------------------------------------------------------------------------- */
/* --- VC-1 Bit Planes                                                   --- */
/* ------------------------------------------------------------------------- */
GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiBitPlane, gst_vaapi_bitplane);
/*GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiBitPlane,
                            gst_vaapi_bitplane)*/

static void
gst_vaapi_bitplane_destroy(GstVaapiBitPlane *bitplane)
{
    vaapi_destroy_buffer(GET_VA_DISPLAY(bitplane), &bitplane->data_id);
    bitplane->data = NULL;
}

static gboolean
gst_vaapi_bitplane_create(
    GstVaapiBitPlane                         *bitplane,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return vaapi_create_buffer(GET_VA_DISPLAY(bitplane),
                               GET_VA_CONTEXT(bitplane),
                               VABitPlaneBufferType,
                               args->param_size,
                               args->param,
                               &bitplane->data_id,
                               (void **)&bitplane->data);
}

GstVaapiBitPlane *
gst_vaapi_bitplane_new(GstVaapiDecoder *decoder, guint8 *data, guint data_size)
{
    GstVaapiBitPlane *obj;
    GstVaapiCodecObjectConstructorArgs args;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    obj = g_slice_new0(GstVaapiBitPlane);
    if (!obj)
        return NULL;

    gst_mini_object_init (GST_MINI_OBJECT_CAST (obj), GST_VAAPI_TYPE_BITPLANE, sizeof(GstVaapiBitPlane));

    obj->parent_instance.free =
      (GstMiniObjectFreeFunction) gst_vaapi_bitplane_destroy;

    obj->args.codec      = GST_VAAPI_CODEC_BASE(decoder);
    obj->args.param      = data;
    obj->args.param_size = data_size;
    obj->args.data       = NULL;
    obj->args.data_size  = 0;
    obj->args.flags      = 0;

    if (!gst_vaapi_bitplane_create (obj, &(obj->args)))
	return NULL;
    return GST_VAAPI_BITPLANE_CAST(obj);
}

#if USE_JPEG_DECODER
GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiHuffmanTable, gst_vaapi_huffman_table);
/*GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiHuffmanTable,
                            gst_vaapi_huffman_table,
                            GST_VAAPI_TYPE_CODEC_OBJECT)*/

static void
gst_vaapi_huffman_table_destroy(GstVaapiHuffmanTable *huf_table)
{
    vaapi_destroy_buffer(GET_VA_DISPLAY(huf_table), &huf_table->param_id);
    huf_table->param = NULL;
}

static gboolean
gst_vaapi_huffman_table_create(
    GstVaapiHuffmanTable                     *huf_table,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return vaapi_create_buffer(GET_VA_DISPLAY(huf_table),
                               GET_VA_CONTEXT(huf_table),
                               VAHuffmanTableBufferType,
                               args->param_size,
                               args->param,
                               &huf_table->param_id,
                               (void **)&huf_table->param);
}

static void
gst_vaapi_huffman_table_init(GstVaapiHuffmanTable *huf_table)
{
    huf_table->param    = NULL;
    huf_table->param_id = VA_INVALID_ID;
}
GstVaapiHuffmanTable *
gst_vaapi_huffman_table_new(
    GstVaapiDecoder *decoder,
    guint8          *data,
    guint            data_size
)
{
    GstVaapiHuffmanTable *obj;
    GstVaapiCodecObjectConstructorArgs args;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);
	
    obj = g_slice_new0(GstVaapiHuffmanTable);
    if (!obj)
        return NULL;

    gst_mini_object_init (GST_MINI_OBJECT_CAST (obj), GST_VAAPI_TYPE_HUFFMAN_TABLE, sizeof(GstVaapiHuffmanTable));

    obj->parent_instance.free =
      (GstMiniObjectFreeFunction) gst_vaapi_huffman_table_destroy;

    obj->args.codec      = GST_VAAPI_CODEC_BASE(decoder);
    obj->args.param      = data;
    obj->args.param_size = data_size;
    obj->args.data       = NULL;
    obj->args.data_size  = 0;
    obj->args.flags      = 0;

    if (!gst_vaapi_huffman_table_create (obj, &(obj->args)))
        return NULL;
    return GST_VAAPI_HUFFMAN_TABLE_CAST(obj);

}
#endif

