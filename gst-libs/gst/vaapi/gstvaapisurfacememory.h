/*
 *  gstvaapisurfacememory.h - Gst VA surface memory
 *
 *  Copyright (C) 2012 Intel Corporation
 *  Contact : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef GST_VAAPI_SURFACE_MEMORY_H
#define GST_VAAPI_SURFACE_MEMORY_H

#include <gst/vaapi/gstvaapisurface.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

G_BEGIN_DECLS

typedef struct _GstVaapiSurfaceMemory GstVaapiSurfaceMemory;

struct _GstVaapiSurfaceMemory {
    GstMemory memory;

    GstVaapiSurface *surface;
    GstVaapiDisplay *display;
    gsize slice_size;
    gpointer user_data;
    GDestroyNotify notify;

    GstVideoInfo        *info;

    GstVaapiChromaType  chroma_type;
    GstVaapiImageFormat ycbcr_format;

    /* Cached data for mapping, copied from gstvdpvideomemory */
    GstMapFlags        map_flags;
    guint              n_planes;
    guint8            *cache;
    void *             cached_data[4];
    gint               destination_pitches[4];
};

#define  GST_VAAPI_SURFACE_MEMORY_NAME  "GstVaapiSurfaceMemory"

/*------------- SurfaceAllocator -----------------*/

#define GST_VAAPI_TYPE_SURFACE_ALLOCATOR        (gst_vaapi_surface_allocator_get_type())
#define GST_IS_VAAPI_SURFACE_ALLOCATOR(obj)     (GST_IS_OBJECT_TYPE(obj, GST_VAAPI_TYPE_SURFACE_ALLOCATOR))
#define GST_VAAPI_SURFACE_ALLOCATOR_CAST(obj)   ((GstVaapiSurfaceAllocator *)(obj))
#define GST_VAAPI_SURFACE_ALLOCATOR(obj)        (GST_VAAPI_SURFACE_ALLOCATOR_CAST(obj))

#define  GST_VAAPI_SURFACE_ALLOCATOR_NAME  "GstVaapiSurfaceAllocator"

typedef struct _GstVaapiSurfaceAllocator GstVaapiSurfaceAllocator;
typedef struct _GstVaapiSurfaceAllocatorClass GstVaapiSurfaceAllocatorClass;

struct _GstVaapiSurfaceAllocator {
    GstAllocator parent;
};

struct _GstVaapiSurfaceAllocatorClass
{
    GstAllocatorClass parent_class;
};

gboolean
gst_vaapi_surface_memory_new (GstVaapiDisplay *display, GstVideoInfo *info);

GType
gst_vaapi_surface_allocator_get_type (void);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_MEMORY_H */

