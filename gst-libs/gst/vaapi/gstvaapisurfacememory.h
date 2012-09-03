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
#include <gst/vaapi/gstvaapivideopool.h>
#include <gst/video/gstvideometa.h>

G_BEGIN_DECLS

/** For internal use **/
typedef struct _SurfaceAllocatorParams SurfaceAllocatorParams;

struct _SurfaceAllocatorParams {
  GstVaapiDisplay *display;
  GstCaps *caps;
  guint width;
  guint height;
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
    SurfaceAllocatorParams allocator_params;
};

struct _GstVaapiSurfaceAllocatorClass
{
    GstAllocatorClass parent_class;
};

gboolean
gst_vaapi_surface_memory_init(GstVaapiDisplay *display, GstCaps *caps);

GType
gst_vaapi_surface_allocator_get_type (void);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_MEMORY_H */

