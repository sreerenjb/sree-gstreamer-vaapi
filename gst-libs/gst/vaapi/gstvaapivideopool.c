/*
 *  gstvaapivideopool.c - Video object pool abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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
 * SECTION:gstvaapivideopool
 * @short_description: Video object pool abstraction
 */

#include "sysdeps.h"
#include "gstvaapivideopool.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiVideoPool, gst_vaapi_video_pool, GST_TYPE_BUFFER_POOL);

#define GST_VAAPI_VIDEO_POOL_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_POOL,	\
                                 GstVaapiVideoPoolPrivate))

struct _GstVaapiVideoPoolPrivate {
    GstVaapiDisplay    *display;
    GstCaps            *caps;
};

enum {
    PROP_0,
    PROP_DISPLAY,
    PROP_CAPS,
};

static void
gst_vaapi_video_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps);

static void
gst_vaapi_video_pool_destroy(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate * const priv = pool->priv;

    if (priv->caps) {
        gst_caps_unref(priv->caps);
        priv->caps = NULL;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }
}

static void
gst_vaapi_video_pool_finalize(GObject *object)
{
    gst_vaapi_video_pool_destroy(GST_VAAPI_VIDEO_POOL(object));

    G_OBJECT_CLASS(gst_vaapi_video_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_video_pool_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiVideoPool * const pool = GST_VAAPI_VIDEO_POOL(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        pool->priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_CAPS:
        gst_vaapi_video_pool_set_caps(pool, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_video_pool_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiVideoPool * const pool = GST_VAAPI_VIDEO_POOL(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, gst_vaapi_video_pool_get_display(pool));
        break;
    case PROP_CAPS:
        g_value_set_pointer(value, gst_vaapi_video_pool_get_caps(pool));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_video_pool_class_init(GstVaapiVideoPoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiVideoPoolPrivate));

    object_class->finalize      = gst_vaapi_video_pool_finalize;
    object_class->set_property  = gst_vaapi_video_pool_set_property;
    object_class->get_property  = gst_vaapi_video_pool_get_property;

    /**
     * GstVaapiVideoPool:display:
     *
     * The #GstVaapiDisplay this pool is bound to.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this pool is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiVidePool:caps:
     *
     * The video object capabilities represented as a #GstCaps. This
     * shall hold at least the "width" and "height" properties.
     */
    g_object_class_install_property
        (object_class,
         PROP_CAPS,
         g_param_spec_pointer("caps",
                              "caps",
                              "The video object capabilities",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

}

static void
gst_vaapi_video_pool_init(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate *priv = GST_VAAPI_VIDEO_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->display       = NULL;
    priv->caps          = NULL;

}

/**
 * gst_vaapi_video_pool_get_display:
 * @pool: a #GstVaapiVideoPool
 *
 * Retrieves the #GstVaapiDisplay the @pool is bound to. The @pool
 * owns the returned object and it shall not be unref'ed.
 *
 * Return value: the #GstVaapiDisplay the @pool is bound to
 */
GstVaapiDisplay *
gst_vaapi_video_pool_get_display(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    return pool->priv->display;
}

/**
 * gst_vaapi_video_pool_get_caps:
 * @pool: a #GstVaapiVideoPool
 *
 * Retrieves the #GstCaps the @pool was created with. The @pool owns
 * the returned object and it shall not be unref'ed.
 *
 * Return value: the #GstCaps the @pool was created with
 */
GstCaps *
gst_vaapi_video_pool_get_caps(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    return pool->priv->caps;
}

/*
 * gst_vaapi_video_pool_set_caps:
 * @pool: a #GstVaapiVideoPool
 * @caps: a #GstCaps
 *
 * Binds new @caps to the @pool and notify the sub-classes.
 */
void
gst_vaapi_video_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps)
{
    GstVaapiVideoPoolClass * const klass = GST_VAAPI_VIDEO_POOL_GET_CLASS(pool);

    pool->priv->caps = gst_caps_ref(caps);

    if (klass->set_caps)
        klass->set_caps(pool, caps);
}

