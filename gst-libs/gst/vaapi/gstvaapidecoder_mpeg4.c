/*
 *  gstvaapidecoder_mpeg4.c - MPEG-4 decoder
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

/**
 * SECTION:gstvaapidecoder_mpeg4
 * @short_description: MPEG-4 decoder, include h263/divx/xvid support
 */

#include "sysdeps.h"
#include <string.h>
#include <gst/base/gstbitreader.h>
#include <gst/codecparsers/gstmpeg4parser.h>
#include "gstvaapidecoder_mpeg4.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoderMpeg4,
              gst_vaapi_decoder_mpeg4,
              GST_VAAPI_TYPE_DECODER)

#define GST_VAAPI_DECODER_MPEG4_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_MPEG4,  \
                                 GstVaapiDecoderMpeg4Private))

struct _GstVaapiDecoderMpeg4Private {
    GstVaapiProfile                 profile;
    guint                           level; 
    guint                           width; 
    guint                           height;
    guint                           fps_n;
    guint                           fps_d; 
    guint                           coding_type;
    GstMpeg4VisualObjectSequence    vos_hdr;
    GstMpeg4VisualObject            vo_hdr;
    GstMpeg4VideoSignalType         signal_type;
    GstMpeg4VideoObjectLayer        vol_hdr;
    GstMpeg4VideoObjectPlane        vop_hdr;
    GstMpeg4VideoPlaneShortHdr      svh_hdr;
    GstMpeg4VideoPacketHdr          packet_hdr;
    GstMpeg4SpriteTrajectory        sprite_trajectory;
    VAIQMatrixBufferMPEG4           iq_matrix;
    GstVaapiPicture                *curr_picture;
    // forward reference pic
    GstVaapiPicture                *next_picture;
    // backward reference pic
    GstVaapiPicture                *prev_picture;
    GstAdapter                     *adapter;
    GstBuffer                      *sub_buffer;
    GstClockTime                    seq_pts;
    GstClockTime                    gop_pts;
    GstClockTime                    pts_diff;
    GstClockTime                    max_pts;
    // anchor sync time base for any picture type, 
    // it is time base of backward reference frame
    GstClockTime                    last_sync_time; 
    // time base for recent I/P/S frame, 
    // it is time base of forward reference frame for B frame
    GstClockTime                    sync_time; 

    /* last non-b-frame time by resolution */
    GstClockTime                    last_non_b_scale_time;
    GstClockTime                    non_b_scale_time;
    GstClockTime                    trb;
    GstClockTime                    trd;
    // temporal_reference of previous frame of svh
    guint8                          prev_t_ref;
    guint                           is_constructed          : 1;
    guint                           is_opened               : 1;
    guint                           is_first_field          : 1;
    guint                           size_changed            : 1;
    guint                           profile_changed         : 1;
    guint                           progressive_sequence    : 1;
    guint                           closed_gop              : 1;
    guint                           broken_link             : 1;
    guint                           calculate_pts_diff      : 1;
    guint                           is_svh                  : 1;
};

static void
gst_vaapi_decoder_mpeg4_close(GstVaapiDecoderMpeg4 *decoder)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->curr_picture, NULL);
    gst_vaapi_picture_replace(&priv->next_picture, NULL);
    gst_vaapi_picture_replace(&priv->prev_picture, NULL);

    if (priv->sub_buffer) {
        gst_buffer_unref(priv->sub_buffer);
        priv->sub_buffer = NULL;
    }

    if (priv->adapter) {
        gst_adapter_clear(priv->adapter);
        g_object_unref(priv->adapter);
        priv->adapter = NULL;
    }
}

static gboolean
gst_vaapi_decoder_mpeg4_open(GstVaapiDecoderMpeg4 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER(decoder);
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstCaps *caps = NULL;
    GstStructure *structure = NULL;

    gst_vaapi_decoder_mpeg4_close(decoder);

    priv->adapter = gst_adapter_new();
    if (!priv->adapter)
        return FALSE;

    priv->is_svh = 0;
    caps = gst_vaapi_decoder_get_caps(base_decoder);
    if (caps) {
        structure = gst_caps_get_structure(caps, 0);
        if (structure) {
            if (gst_structure_has_name(structure, "video/x-h263")) {
                priv->is_svh = 1;
                priv->profile = GST_VAAPI_PROFILE_MPEG4_SIMPLE;
                priv->prev_t_ref = -1;
            }
        }
    }
    return TRUE;
}

static void
gst_vaapi_decoder_mpeg4_destroy(GstVaapiDecoderMpeg4 *decoder)
{
    gst_vaapi_decoder_mpeg4_close(decoder);
}

static gboolean
gst_vaapi_decoder_mpeg4_create(GstVaapiDecoderMpeg4 *decoder)
{
    if (!GST_VAAPI_DECODER_CODEC(decoder))
        return FALSE;
    return TRUE;
}

static inline void
copy_quant_matrix(guint8 dst[64], const guint8 src[64])
{
    memcpy(dst, src, 64);
}

static GstVaapiDecoderStatus
ensure_context(GstVaapiDecoderMpeg4 *decoder)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstVaapiProfile profiles[2];
    GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
    guint i, n_profiles = 0;
    gboolean reset_context = FALSE;

    if (priv->profile_changed) {
        GST_DEBUG("profile changed");
        priv->profile_changed = FALSE;
        reset_context         = TRUE;

        profiles[n_profiles++] = priv->profile;
        if (priv->profile == GST_VAAPI_PROFILE_MPEG4_SIMPLE)
            profiles[n_profiles++] = GST_VAAPI_PROFILE_MPEG4_ADVANCED_SIMPLE;

        for (i = 0; i < n_profiles; i++) {
            if (gst_vaapi_display_has_decoder(GST_VAAPI_DECODER_DISPLAY(decoder),
                                              profiles[i], entrypoint))
                break;
        }
        if (i == n_profiles)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
        priv->profile = profiles[i];
    }

    if (priv->size_changed) {
        GST_DEBUG("size changed");
        priv->size_changed = FALSE;
        reset_context      = TRUE;
    }

    if (reset_context) {
        GstVaapiContextInfo info;

        info.profile    = priv->profile;
        info.entrypoint = entrypoint;
        info.width      = priv->width;
        info.height     = priv->height;
        info.ref_frames = 2;
        reset_context   = gst_vaapi_decoder_ensure_context(
            GST_VAAPI_DECODER(decoder),
            &info
        );
        if (!reset_context)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
ensure_quant_matrix(GstVaapiDecoderMpeg4 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    VAIQMatrixBufferMPEG4 *iq_matrix;

    if (!priv->vol_hdr.load_intra_quant_mat && !priv->vol_hdr.load_non_intra_quant_mat) {
            return GST_VAAPI_DECODER_STATUS_SUCCESS;
    }

    picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(MPEG4, decoder);
    if (!picture->iq_matrix) {
        GST_DEBUG("failed to allocate IQ matrix");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    iq_matrix = picture->iq_matrix->param;

    if (priv->vol_hdr.load_intra_quant_mat) {
        iq_matrix->load_intra_quant_mat = 1;
        copy_quant_matrix(iq_matrix->intra_quant_mat,
                          priv->vol_hdr.intra_quant_mat);
    }
    else
        iq_matrix->load_intra_quant_mat = 0;

    if (priv->vol_hdr.load_non_intra_quant_mat) {
        iq_matrix->load_non_intra_quant_mat = 1;
        copy_quant_matrix(iq_matrix->non_intra_quant_mat,
                      priv->vol_hdr.non_intra_quant_mat);
    }
    else
        iq_matrix->load_non_intra_quant_mat = 0;
    

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline GstVaapiDecoderStatus
render_picture(GstVaapiDecoderMpeg4 *decoder, GstVaapiPicture *picture)
{
    if (!gst_vaapi_picture_output(picture))
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* decode_picture() start to decode a frame/picture
 * decode_current_picture() finishe decoding a frame/picture 
 * (commit buffer to driver for decoding)
 */
static GstVaapiDecoderStatus
decode_current_picture(GstVaapiDecoderMpeg4 *decoder)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->curr_picture;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (picture) {
        if (!gst_vaapi_picture_decode(picture))
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
            if ((priv->prev_picture && priv->next_picture) ||
                (priv->closed_gop && priv->next_picture))
                status = render_picture(decoder, picture);
        }
        gst_vaapi_picture_replace(&priv->curr_picture, NULL);
    }
    return status;
}

static GstVaapiDecoderStatus
decode_sequence(GstVaapiDecoderMpeg4 *decoder, const guint8 *buf, guint buf_size)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstMpeg4VisualObjectSequence * const vos_hdr = &priv->vos_hdr;
    GstVaapiProfile profile;

    if (gst_mpeg4_parse_visual_object_sequence(vos_hdr, buf, buf_size) != GST_MPEG4_PARSER_OK) {
        GST_DEBUG("failed to parse sequence header");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    priv->level = vos_hdr->level;
    switch (vos_hdr->profile) {
    case GST_MPEG4_PROFILE_SIMPLE:
        profile = GST_VAAPI_PROFILE_MPEG4_SIMPLE;
        break;
    case GST_MPEG4_PROFILE_ADVANCED_SIMPLE:
    case GST_MPEG4_PROFILE_SIMPLE_SCALABLE: /* shared profile with ADVANCED_SIMPLE */
        profile = GST_VAAPI_PROFILE_MPEG4_ADVANCED_SIMPLE;
        break;
    default:
        GST_DEBUG("unsupported profile %d", vos_hdr->profile);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }
    if (priv->profile != profile) {
        priv->profile = profile;
        priv->profile_changed = TRUE;
    }
    priv->seq_pts = gst_adapter_prev_timestamp(priv->adapter, NULL);
    priv->size_changed          = TRUE;

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end(GstVaapiDecoderMpeg4 *decoder)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;

    if (priv->curr_picture) {
        status = decode_current_picture(decoder);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
        status = render_picture(decoder, priv->curr_picture);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }

    if (priv->next_picture) {
        status = render_picture(decoder, priv->next_picture);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }
    return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
}

static GstVaapiDecoderStatus
decode_visual_object(GstVaapiDecoderMpeg4 *decoder, const guint8 *buf, guint buf_size)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstMpeg4VisualObject * vo_hdr = &priv->vo_hdr;
    GstMpeg4VideoSignalType * signal_type = &priv->signal_type;

    if (gst_mpeg4_parse_visual_object (vo_hdr, signal_type, buf, buf_size) != GST_MPEG4_PARSER_OK) {
        GST_DEBUG("failed to parse visual object");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    /* XXX: video_signal_type isn't used for decoding */
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_video_object_layer(GstVaapiDecoderMpeg4 *decoder, const guint8 *buf, guint buf_size)
{
    GstVaapiDecoder * const base_decoder = GST_VAAPI_DECODER(decoder);
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstMpeg4VisualObject * vo_hdr = &priv->vo_hdr;
    GstMpeg4VideoObjectLayer * vol_hdr = &priv->vol_hdr;

    if (gst_mpeg4_parse_video_object_layer (vol_hdr, vo_hdr, buf, buf_size) != GST_MPEG4_PARSER_OK) {
        GST_DEBUG("failed to parse video object layer");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    priv->width  = vol_hdr->width;
    priv->height = vol_hdr->height;

    priv->progressive_sequence  = !vol_hdr->interlaced;

    if (vol_hdr->fixed_vop_rate) {
        priv->fps_n = vol_hdr->vop_time_increment_resolution;
        priv->fps_d = vol_hdr->fixed_vop_time_increment;
        gst_vaapi_decoder_set_framerate(base_decoder, priv->fps_n, priv->fps_d);    
    }

    gst_vaapi_decoder_set_pixel_aspect_ratio(base_decoder, priv->vol_hdr.par_width, priv->vol_hdr.par_height);
    gst_vaapi_decoder_set_picture_size(base_decoder, priv->width, priv->height);

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_gop(GstVaapiDecoderMpeg4 *decoder, const guint8 *buf, guint buf_size)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstMpeg4GroupOfVOP gop;
    GstClockTime gop_time;

    if (buf_size >4) {
        if (gst_mpeg4_parse_group_of_vop(&gop, buf, buf_size) != GST_MPEG4_PARSER_OK) {
        GST_DEBUG("failed to parse GOP");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        }
    }
    else {
        gop.closed          = 1;
        gop.broken_link     = 0;
        gop.hours           = 0;
        gop.minutes         = 0;
        gop.seconds         = 0;
    }

    priv->closed_gop  = gop.closed;
    priv->broken_link = gop.broken_link;

    GST_DEBUG("GOP %02u:%02u:%02u (closed_gop %d, broken_link %d)",
              gop.hours, gop.minutes, gop.seconds,
              priv->closed_gop, priv->broken_link);

    gop_time             = gop.hours * 3600 + gop.minutes * 60 + gop.seconds;
    priv->last_sync_time = gop_time;
    priv->sync_time      = gop_time;
    
    if (priv->gop_pts != GST_CLOCK_TIME_NONE)
        priv->pts_diff += gop_time * GST_SECOND - priv->gop_pts;
    priv->gop_pts = gop_time * GST_SECOND;
    priv->calculate_pts_diff = TRUE;
    priv->is_first_field = TRUE;

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

void
calculate_pts_diff(GstVaapiDecoderMpeg4 *decoder,
                      GstMpeg4VideoObjectLayer *vol_hdr,
                      GstMpeg4VideoObjectPlane *vop_hdr)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstClockTime frame_timestamp;

    frame_timestamp = gst_adapter_prev_timestamp(priv->adapter, NULL);
    if (frame_timestamp && frame_timestamp != GST_CLOCK_TIME_NONE) {
        /* Buffer with timestamp */
        if (priv->max_pts != GST_CLOCK_TIME_NONE &&
            frame_timestamp < priv->max_pts) {
            frame_timestamp = priv->max_pts +
                gst_util_uint64_scale((vol_hdr->fixed_vop_rate ?
                                       vol_hdr->fixed_vop_time_increment : 1),
                                      GST_SECOND,
                                      vol_hdr->vop_time_increment_resolution);
        }
    } else {
        /* Buffer without timestamp set */
        if (priv->max_pts == GST_CLOCK_TIME_NONE) /* first buffer */
            frame_timestamp = 0;
        else {
            GstClockTime tmp_pts;
            tmp_pts = priv->pts_diff + priv->gop_pts +
                vop_hdr->modulo_time_base * GST_SECOND +
                gst_util_uint64_scale(vop_hdr->time_increment,
                                      GST_SECOND,
                                      vol_hdr->vop_time_increment_resolution);
            if (tmp_pts > priv->max_pts)
                frame_timestamp = tmp_pts;
            else
                frame_timestamp = priv->max_pts +
                    gst_util_uint64_scale((vol_hdr->fixed_vop_rate ?
                                           vol_hdr->fixed_vop_time_increment : 1),
                                           GST_SECOND,
                                          vol_hdr->vop_time_increment_resolution);
        }
    }

    priv->pts_diff = frame_timestamp -
        (priv->gop_pts + vop_hdr->modulo_time_base * GST_SECOND +
         gst_util_uint64_scale(vop_hdr->time_increment, GST_SECOND,
                               vol_hdr->vop_time_increment_resolution));
}
 
static GstVaapiDecoderStatus
decode_picture(GstVaapiDecoderMpeg4 *decoder, const guint8 *buf, guint buf_size)
{
    GstMpeg4ParseResult parser_result = GST_MPEG4_PARSER_OK;
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstMpeg4VideoObjectPlane * const vop_hdr = &priv->vop_hdr;
    GstMpeg4VideoObjectLayer * const vol_hdr = &priv->vol_hdr;
    GstMpeg4SpriteTrajectory * const sprite_trajectory = &priv->sprite_trajectory;
    GstVaapiPicture *picture;
    GstVaapiDecoderStatus status;
    GstClockTime pts;

    // context depends on priv->width and priv->height, so we move parse_vop a little earlier
    if (priv->is_svh) {
        parser_result = gst_mpeg4_parse_video_plane_short_header(&priv->svh_hdr, buf, buf_size);

    }
    else {
        parser_result = gst_mpeg4_parse_video_object_plane(vop_hdr, sprite_trajectory, vol_hdr, buf, buf_size);
        /* Need to skip this frame if VOP was not coded */
        if (GST_MPEG4_PARSER_OK == parser_result && !vop_hdr->coded)
            return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    }

    if (parser_result != GST_MPEG4_PARSER_OK) {
        GST_DEBUG("failed to parse picture header");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    if (priv->is_svh) {
        priv->width = priv->svh_hdr.vop_width;
        priv->height = priv->svh_hdr.vop_height;
    }
    else {
        if (!vop_hdr->width && !vop_hdr->height) {
            vop_hdr->width = vol_hdr->width;
            vop_hdr->height = vol_hdr->height;
        }
        priv->width = vop_hdr->width;
        priv->height = vop_hdr->height;
    }

    status = ensure_context(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_DEBUG("failed to reset context");
        return status;
    }

    if (priv->curr_picture) {
        status = decode_current_picture(decoder);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }

    priv->curr_picture = GST_VAAPI_PICTURE_NEW(MPEG4, decoder);
    if (!priv->curr_picture) {
        GST_DEBUG("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    picture = priv->curr_picture;

    status = ensure_quant_matrix(decoder, picture);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_DEBUG("failed to reset quantizer matrix");
        return status;
    }

    /* 7.6.7 Temporal prediction structure
     * forward reference frame     B B B B B B      backward reference frame
     *            |                                              |
     *  nearest I/P/S in the past with vop_coded ==1             |
     *                         nearest I/P/S in the future with any vop_coded
     * fixme, it said that B frame shouldn't use backward reference frame 
     *        when backward reference frame coded is 0
     */
    if (priv->is_svh) {
        priv->coding_type = priv->svh_hdr.picture_coding_type;
    }
    else {
        priv->coding_type = priv->vop_hdr.coding_type;
    }
    switch (priv->coding_type) {
    case GST_MPEG4_I_VOP:
        picture->type = GST_VAAPI_PICTURE_TYPE_I;
        if (priv->is_svh || vop_hdr->coded) 
            GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        break;
    case GST_MPEG4_P_VOP:
        picture->type = GST_VAAPI_PICTURE_TYPE_P;
        if (priv->is_svh || vop_hdr->coded) 
            GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        break;
    case GST_MPEG4_B_VOP:
        picture->type = GST_VAAPI_PICTURE_TYPE_B;
        break;
    case GST_MPEG4_S_VOP:
        picture->type = GST_VAAPI_PICTURE_TYPE_S;
        // see 3.175 reference VOP
        if (vop_hdr->coded) 
            GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        break;
    default:
        GST_DEBUG("unsupported picture type %d", priv->coding_type);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    if (!priv->is_svh && !vop_hdr->coded) {
        status = render_picture(decoder, priv->prev_picture);
        return status;
    }

    if (priv->is_svh) {
        guint temp_ref = priv->svh_hdr.temporal_reference;
        if (temp_ref < priv->prev_t_ref) {
            temp_ref += 256;
        }
        guint delta_ref = temp_ref - priv->prev_t_ref;

        pts = priv->sync_time;
        // see temporal_reference definition in spec, 30000/1001Hz
        pts += gst_util_uint64_scale(delta_ref, GST_SECOND*1001, 30000);
        priv->sync_time = pts;
        priv->prev_t_ref = priv->svh_hdr.temporal_reference;
    }
    else {
        /* Update priv->pts_diff */
        if (priv->calculate_pts_diff) {
            calculate_pts_diff(decoder, vol_hdr, vop_hdr);
            priv->calculate_pts_diff = FALSE;
        }

        /* Update presentation time, 6.3.5 */
        if(vop_hdr->coding_type != GST_MPEG4_B_VOP) {
            // increment basing on decoding order
            priv->last_sync_time = priv->sync_time;
            priv->sync_time = priv->last_sync_time + vop_hdr->modulo_time_base;
            pts = priv->sync_time * GST_SECOND;
            pts += gst_util_uint64_scale(vop_hdr->time_increment, GST_SECOND, vol_hdr->vop_time_increment_resolution);
            priv->last_non_b_scale_time = priv->non_b_scale_time;
            priv->non_b_scale_time = priv->sync_time * vol_hdr->vop_time_increment_resolution + vop_hdr->time_increment;
            priv->trd  = priv->non_b_scale_time - priv->last_non_b_scale_time;
        }
        else {
            // increment basing on display oder
            pts = (priv->last_sync_time + vop_hdr->modulo_time_base) * GST_SECOND;
            pts += gst_util_uint64_scale(vop_hdr->time_increment, GST_SECOND, vol_hdr->vop_time_increment_resolution);
            priv->trb = (priv->last_sync_time + vop_hdr->modulo_time_base) * vol_hdr->vop_time_increment_resolution +
                vop_hdr->time_increment - priv->last_non_b_scale_time;
        }
    }
    picture->pts = pts + priv->pts_diff;
    if (priv->max_pts == GST_CLOCK_TIME_NONE || priv->max_pts < picture->pts)
        priv->max_pts = picture->pts;

    /* Update reference pictures */
    /* XXX: consider priv->vol_hdr.low_delay, consider packed video frames for DivX/XviD */
    if (GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
        if (priv->next_picture)
            status = render_picture(decoder, priv->next_picture);
        gst_vaapi_picture_replace(&priv->prev_picture, priv->next_picture);
        gst_vaapi_picture_replace(&priv->next_picture, picture);
    }
    return status;
}

static inline guint
get_vop_coding_type(GstVaapiPicture *picture)
{
    return picture->type - GST_VAAPI_PICTURE_TYPE_I;
}

static gboolean
fill_picture(GstVaapiDecoderMpeg4 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    VAPictureParameterBufferMPEG4 * const pic_param = picture->param;
    GstMpeg4VideoObjectPlane * const vop_hdr = &priv->vop_hdr;

    /* Fill in VAPictureParameterBufferMPEG4 */
    pic_param->forward_reference_picture                        = VA_INVALID_ID;
    pic_param->backward_reference_picture                       = VA_INVALID_ID;

    pic_param->vol_fields.value                                 = 0;
    pic_param->vop_fields.value                                 = 0;
    if(priv->is_svh) {
        // vol_hdr Parameters
        pic_param->vol_fields.bits.short_video_header           = 1; 
        // does the following vol_hdr parameters matter for short video header?
        pic_param->vol_fields.bits.chroma_format                = 1; // I420, see table 6-15. 
        pic_param->vol_fields.bits.interlaced                   = 0; 
        pic_param->vol_fields.bits.obmc_disable                 = 1;
        pic_param->vol_fields.bits.sprite_enable                = 0;
        pic_param->vol_fields.bits.sprite_warping_accuracy      = 0;
        pic_param->vol_fields.bits.quant_type                   = 0; //method 1; $7.4.4
        pic_param->vol_fields.bits.quarter_sample               = 0; 
        pic_param->vol_fields.bits.data_partitioned             = 0; 
        pic_param->vol_fields.bits.reversible_vlc               = 0; 
        pic_param->vol_fields.bits.resync_marker_disable        = 1; 
        pic_param->no_of_sprite_warping_points                  = 0; 
        pic_param->quant_precision                              = 5;
        // VOP parameters    
        pic_param->vop_width                                    = priv->svh_hdr.vop_width;
        pic_param->vop_height                                   = priv->svh_hdr.vop_height;
        pic_param->vop_fields.bits.vop_coding_type              = priv->svh_hdr.picture_coding_type;
        pic_param->vop_time_increment_resolution                = priv->vol_hdr.vop_time_increment_resolution; 
        
        pic_param->num_gobs_in_vop                              = priv->svh_hdr.num_gobs_in_vop;
        pic_param->num_macroblocks_in_gob                       = priv->svh_hdr.num_macroblocks_in_gob;
    }
    else {
        // VOL parameters
        pic_param->vol_fields.bits.short_video_header           = 0; 
        pic_param->vol_fields.bits.chroma_format                = priv->vol_hdr.chroma_format;
        pic_param->vol_fields.bits.interlaced                   = priv->vol_hdr.interlaced;
        pic_param->vol_fields.bits.obmc_disable                 = priv->vol_hdr.obmc_disable;
        pic_param->vol_fields.bits.sprite_enable                = priv->vol_hdr.sprite_enable;
        pic_param->vol_fields.bits.sprite_warping_accuracy      = priv->vol_hdr.sprite_warping_accuracy; 
        pic_param->vol_fields.bits.quant_type                   = priv->vol_hdr.quant_type;
        pic_param->vol_fields.bits.quarter_sample               = priv->vol_hdr.quarter_sample;
        pic_param->vol_fields.bits.data_partitioned             = priv->vol_hdr.data_partitioned;
        pic_param->vol_fields.bits.reversible_vlc               = priv->vol_hdr.reversible_vlc;
        pic_param->vol_fields.bits.resync_marker_disable        = priv->vol_hdr.resync_marker_disable;
        pic_param->no_of_sprite_warping_points                  = priv->vol_hdr.no_of_sprite_warping_points;
        int i =0;
        for (i=0; i<3 && i<priv->vol_hdr.no_of_sprite_warping_points ; i++) {
            pic_param->sprite_trajectory_du[i]                  = priv->sprite_trajectory.vop_ref_points[i];
            pic_param->sprite_trajectory_dv[i]                  = priv->sprite_trajectory.sprite_ref_points[i];
        }
        pic_param->quant_precision                              = priv->vol_hdr.quant_precision;
        
        // VOP parameters    
        pic_param->vop_width                                    = vop_hdr->width;
        pic_param->vop_height                                   = vop_hdr->height;
        pic_param->vop_fields.bits.vop_coding_type              = vop_hdr->coding_type;
        pic_param->vop_fields.bits.vop_rounding_type            = vop_hdr->rounding_type;
        pic_param->vop_fields.bits.intra_dc_vlc_thr             = vop_hdr->intra_dc_vlc_thr;
        pic_param->vop_fields.bits.top_field_first              = vop_hdr->top_field_first;
        pic_param->vop_fields.bits.alternate_vertical_scan_flag = vop_hdr->alternate_vertical_scan_flag;

        pic_param->vop_fcode_forward                            = vop_hdr->fcode_forward;
        pic_param->vop_fcode_backward                           = vop_hdr->fcode_backward;
        pic_param->vop_time_increment_resolution                = priv->vol_hdr.vop_time_increment_resolution;
    }    

    pic_param->TRB = 0;
    pic_param->TRD = 0;
    switch (priv->coding_type) {
    case GST_MPEG4_B_VOP:
        pic_param->TRB                                          = priv->trb;
        pic_param->backward_reference_picture                   = priv->next_picture->surface_id;
        pic_param->vop_fields.bits.backward_reference_vop_coding_type = get_vop_coding_type(priv->next_picture);
        // fall-through
    case GST_MPEG4_P_VOP:
        pic_param->TRD                                          = priv->trd;
        if (priv->prev_picture)
            pic_param->forward_reference_picture                = priv->prev_picture->surface_id;
        break;
    }

    if (priv->vol_hdr.interlaced) {
        priv->is_first_field ^= 1;
    }
    return TRUE;
}

static GstVaapiDecoderStatus
decode_slice(
    GstVaapiDecoderMpeg4 *decoder,
    const guint8          *buf,
    guint                 buf_size,
    gboolean              has_packet_header
)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->curr_picture;
    GstVaapiSlice *slice;
    VASliceParameterBufferMPEG4 *slice_param;

    GST_DEBUG("decoder silce: %p, %u bytes)", buf, buf_size);

    // has_packet_header is ture for the 2+ slice
    if (!has_packet_header && !fill_picture(decoder, picture))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    slice = GST_VAAPI_SLICE_NEW(MPEG4, decoder, buf, buf_size);
    if (!slice) {
        GST_DEBUG("failed to allocate slice");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    gst_vaapi_picture_add_slice(picture, slice);

    /* Fill in VASliceParameterBufferMPEG4 */
    slice_param = slice->param;
    if (priv->is_svh) {
        slice_param->macroblock_offset         = (priv->svh_hdr.size)%8;
        slice_param->macroblock_number         = 0; 
        // the header of first gob_layer is empty (gob_header_empty=1), use vop_quant
        slice_param->quant_scale               = priv->svh_hdr.vop_quant; 
    }
    else {
        if (has_packet_header) {
            slice_param->macroblock_offset     = priv->packet_hdr.size % 8;
            slice_param->macroblock_number     = priv->packet_hdr.macroblock_number;
            slice_param->quant_scale           = priv->packet_hdr.quant_scale;
       }    
        else {
            slice_param->macroblock_offset     = priv->vop_hdr.size % 8;
            slice_param->macroblock_number     = 0;
            slice_param->quant_scale           = priv->vop_hdr.quant;
        }
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_packet(GstVaapiDecoderMpeg4 *decoder, GstMpeg4Packet packet)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstMpeg4Packet *tos = &packet;
    GstVaapiDecoderStatus status;

    if (tos->size < 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    status = gst_vaapi_decoder_check_status(GST_VAAPI_DECODER(decoder));
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    // packet.size is the size from current marker to the next.
    if (tos->type == GST_MPEG4_VISUAL_OBJ_SEQ_START) {
        status = decode_sequence(decoder, packet.data + packet.offset, packet.size);
    }
    else if (tos->type == GST_MPEG4_VISUAL_OBJ_SEQ_END) {
        status = decode_sequence_end(decoder);
    }
    else if (tos->type == GST_MPEG4_VISUAL_OBJ) {
        status = decode_visual_object(decoder, packet.data + packet.offset, packet.size);
    }
    else if (tos->type >= GST_MPEG4_VIDEO_OBJ_FIRST && tos->type <= GST_MPEG4_VIDEO_OBJ_LAST) {
        GST_WARNING("unexpected marker: (GST_MPEG4_VIDEO_OBJ_FIRST, GST_MPEG4_VIDEO_OBJ_LAST)");
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    }
    else if (tos->type >= GST_MPEG4_VIDEO_LAYER_FIRST && tos->type <= GST_MPEG4_VIDEO_LAYER_LAST) {
        status = decode_video_object_layer(decoder, packet.data + packet.offset, packet.size);
    }
    else if (tos->type == GST_MPEG4_GROUP_OF_VOP) {
        status = decode_gop(decoder, packet.data + packet.offset, packet.size);
    }
    else if (tos->type == GST_MPEG4_VIDEO_OBJ_PLANE) {
        status = decode_picture(decoder, packet.data + packet.offset, packet.size);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;

        /* decode slice
         * A resync marker shall only be located immediately before a macroblock 
         * (or video packet header if exists) and aligned with a byte
         * either start_code or resync_marker are scaned/measured by byte, 
         * while the header itself are parsed/measured in bit
         * it means: resync_marker(video_packet_header) start from byte boundary, 
         * while MB doesn't start from byte boundary -- it is what 'macroblock_offset' 
         * in slice refer to
         */
        const guint8 *_data = packet.data + packet.offset + priv->vop_hdr.size/8; 
        gint  _data_size = packet.size - (priv->vop_hdr.size/8); 
        GstMpeg4Packet video_packet;
        
        if (priv->vol_hdr.resync_marker_disable) {
            status = decode_slice(decoder, _data, _data_size, FALSE);
            if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
                return status;
        }
        else {
            // next start_code is required to determine the end of last slice
            _data_size += 4;
            GstMpeg4ParseResult ret = GST_MPEG4_PARSER_OK;

            gboolean first_slice = TRUE;
            while (_data_size > 0) {
                // we can skip user data here
                ret = gst_mpeg4_parse(&video_packet, TRUE, &priv->vop_hdr, _data, 0,  _data_size);
                if(ret != GST_MPEG4_PARSER_OK) {
                    break;
                }

                if (first_slice) {
                    status = decode_slice(decoder, _data, video_packet.size, FALSE);
                    first_slice = FALSE;
                }
                else {
                    _data += video_packet.offset;
                    _data_size -= video_packet.offset;

                    ret = gst_mpeg4_parse_video_packet_header (&priv->packet_hdr, &priv->vol_hdr, &priv->vop_hdr, &priv->sprite_trajectory, _data, _data_size);
                    status = decode_slice(decoder,_data + priv->packet_hdr.size/8, video_packet.size - priv->packet_hdr.size/8, TRUE); 
                }

                _data += video_packet.size;
                _data_size -= video_packet.size;
            }
        }
        status = decode_current_picture(decoder);
    }
    else if (tos->type == GST_MPEG4_USER_DATA
          || tos->type == GST_MPEG4_VIDEO_SESSION_ERR 
          || tos->type == GST_MPEG4_FBA 
          || tos->type == GST_MPEG4_FBA_PLAN 
          || tos->type == GST_MPEG4_MESH 
          || tos->type == GST_MPEG4_MESH_PLAN 
          || tos->type == GST_MPEG4_STILL_TEXTURE_OBJ 
          || tos->type == GST_MPEG4_TEXTURE_SPATIAL 
          || tos->type == GST_MPEG4_TEXTURE_SNR_LAYER 
          || tos->type == GST_MPEG4_TEXTURE_TILE 
          || tos->type == GST_MPEG4_SHAPE_LAYER 
          || tos->type == GST_MPEG4_STUFFING 
          || tos->type == GST_MPEG4_SYSTEM_FIRST 
          || tos->type == GST_MPEG4_SYSTEM_LAST) {
        GST_WARNING("Ignore marker: %x\n", tos->type);
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    }
    
    return status;
}

static GstVaapiDecoderStatus
decode_buffer(GstVaapiDecoderMpeg4 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    guchar *buf;
    guint pos, buf_size;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);

    // visual object sequence end
    if (!buf && buf_size == 0)
        return decode_sequence_end(decoder);

    gst_buffer_ref(buffer);
    gst_adapter_push(priv->adapter, buffer);

    if (priv->sub_buffer) {
        buffer = gst_buffer_merge(priv->sub_buffer, buffer);
        if (!buffer)
            return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
        gst_buffer_unref(priv->sub_buffer);
        priv->sub_buffer = NULL;
    }

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    pos = 0;

    GstMpeg4Packet packet;
    GstMpeg4ParseResult result = GST_MPEG4_PARSER_OK;
    guint consumed_size = 0;

    if (priv->is_svh) {
        while (result == GST_MPEG4_PARSER_OK && pos < buf_size) {
            result = gst_h263_parse (&packet,buf, pos, buf_size);
            if (result != GST_MPEG4_PARSER_OK) {
                break;
            }
            status = decode_picture(decoder, packet.data+packet.offset, packet.size);
            if (GST_VAAPI_DECODER_STATUS_SUCCESS == status) {
                // MBs are not byte aligned, so we set the start address with byte aligned 
                // and mb offset with (priv->svh_hdr.size)%8
                status = decode_slice(decoder, packet.data+packet.offset+(priv->svh_hdr.size)/8, 
                        packet.size - (priv->svh_hdr.size)/8, FALSE);
                status = decode_current_picture(decoder);

                consumed_size = packet.offset + packet.size; 
                pos += consumed_size; 
                if (gst_adapter_available(priv->adapter) >= pos)
                    gst_adapter_flush(priv->adapter, pos);
            }
            else {
                GST_WARNING("decode h263 packet failed\n");
                break;
            }
        }
    }
    else {
        while (pos < buf_size) {
            // don't skip user data, we need the size to pop tsb buffer
            result = gst_mpeg4_parse(&packet, FALSE, NULL, buf, pos, buf_size);
            if (result != GST_MPEG4_PARSER_OK) {
                break;
            }
            status = decode_packet(decoder, packet);
            if (GST_VAAPI_DECODER_STATUS_SUCCESS == status ||
                GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA == status) {
                consumed_size = packet.offset + packet.size - pos; 
                pos = packet.offset + packet.size; 
                if (gst_adapter_available(priv->adapter) >= pos)
                    gst_adapter_flush(priv->adapter, pos);
            }
            else {
                GST_WARNING("decode mp4 packet failed\n");
                break;
            }
        }
    }

    if ((result == GST_MPEG4_PARSER_NO_PACKET ||
         result == GST_MPEG4_PARSER_NO_PACKET_END ||
         status == GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE) &&
        pos < buf_size) {
        priv->sub_buffer = gst_buffer_create_sub(buffer, pos, buf_size-pos);
        status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    }
    return status;
}

static GstVaapiDecoderStatus
decode_codec_data(GstVaapiDecoderMpeg4 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderStatus status;
    guchar *buf, *_buf;
    guint pos, buf_size, _buf_size;

    _buf      = GST_BUFFER_DATA(buffer);
    _buf_size = GST_BUFFER_SIZE(buffer);
    // add additional 0x000001b2 to enclose the last header
    buf_size = _buf_size + 4;
    buf = malloc(buf_size);
    memcpy(buf, _buf, buf_size);
    buf[buf_size-4] = 0;
    buf[buf_size-3] = 0;
    buf[buf_size-2] = 1;
    buf[buf_size-1] = 0xb2;

    pos = 0;
    GstMpeg4Packet packet;
    GstMpeg4ParseResult result = GST_MPEG4_PARSER_OK;

    while (result == GST_MPEG4_PARSER_OK && pos < buf_size) {
        result = gst_mpeg4_parse(&packet, FALSE, NULL, buf, pos, buf_size);
        if (result != GST_MPEG4_PARSER_OK) {
            break;
        }
        status = decode_packet(decoder, packet);
        if (GST_VAAPI_DECODER_STATUS_SUCCESS == status) {
            pos = packet.offset + packet.size; 
        }
        else {
            GST_WARNING("decode mp4 packet failed when decoding codec data\n");
            break;
        }
    }
    free(buf);
    return status;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg4_decode(GstVaapiDecoder *base, GstBuffer *buffer)
{
    GstVaapiDecoderMpeg4 * const decoder = GST_VAAPI_DECODER_MPEG4(base);
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GstBuffer *codec_data = NULL;
    GstVaapiDecoderStatus status;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_mpeg4_open(decoder, buffer);
        if (!priv->is_opened)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;

        codec_data = GST_VAAPI_DECODER_CODEC_DATA(decoder);
        if (codec_data) {
            status = decode_codec_data(decoder, codec_data);
            if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
                return status;
        }
    }
    return decode_buffer(decoder, buffer);
}

static void
gst_vaapi_decoder_mpeg4_finalize(GObject *object)
{
    GstVaapiDecoderMpeg4 * const decoder = GST_VAAPI_DECODER_MPEG4(object);

    gst_vaapi_decoder_mpeg4_destroy(decoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_mpeg4_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_mpeg4_constructed(GObject *object)
{
    GstVaapiDecoderMpeg4 * const decoder = GST_VAAPI_DECODER_MPEG4(object);
    GstVaapiDecoderMpeg4Private * const priv = decoder->priv;
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_mpeg4_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_mpeg4_create(decoder);
}

static void
gst_vaapi_decoder_mpeg4_class_init(GstVaapiDecoderMpeg4Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderMpeg4Private));

    object_class->finalize      = gst_vaapi_decoder_mpeg4_finalize;
    object_class->constructed   = gst_vaapi_decoder_mpeg4_constructed;

    decoder_class->decode       = gst_vaapi_decoder_mpeg4_decode;
}

static void
gst_vaapi_decoder_mpeg4_init(GstVaapiDecoderMpeg4 *decoder)
{
    GstVaapiDecoderMpeg4Private *priv;

    priv                        = GST_VAAPI_DECODER_MPEG4_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->width                 = 0;
    priv->height                = 0;
    priv->fps_n                 = 0;
    priv->fps_d                 = 0;
    priv->profile               = GST_VAAPI_PROFILE_MPEG4_SIMPLE;
    priv->curr_picture          = NULL;
    priv->next_picture          = NULL;
    priv->prev_picture          = NULL;
    priv->adapter               = NULL;
    priv->sub_buffer            = NULL;
    priv->seq_pts               = GST_CLOCK_TIME_NONE;
    priv->gop_pts               = GST_CLOCK_TIME_NONE;
    priv->max_pts               = GST_CLOCK_TIME_NONE;
    priv->pts_diff              = 0;
    priv->calculate_pts_diff    = TRUE;
    priv->is_constructed        = FALSE;
    priv->is_opened             = FALSE;
    priv->is_first_field        = FALSE;
    priv->size_changed          = TRUE;
    priv->profile_changed       = TRUE;
    priv->progressive_sequence  = FALSE;
    priv->closed_gop            = FALSE;
    priv->broken_link           = FALSE;
    priv->last_non_b_scale_time = 0;
    priv->non_b_scale_time      = 0;
    priv->trb                   = 0;
    priv->trd                   = 0;
}

/**
 * gst_vaapi_decoder_mpeg4_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_mpeg4_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderMpeg4 *decoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    decoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_MPEG4,
        "display",      display,
        "caps",         caps,
        NULL
    );
    if (!decoder->priv->is_constructed) {
        g_object_unref(decoder);
        return NULL;
    }
    return GST_VAAPI_DECODER_CAST(decoder);
}
