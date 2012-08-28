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

#define GST_VAAPI_TYPE_SURFACE_MEMORY           (gst_vaapi_surface_memory_get_type())
#define GST_IS_VAAPI_SURFACE_MEMORY(obj)        (GST_IS_MINI_OBJECT_TYPE(obj, GST_VAAPI_TYPE_SURFACE_MEMORY))
#define GST_VAAPI_SURFACE_MEMORY_CAST(obj)      ((GstVaapiSurfaceMemory *)(obj))
#define GST_VAAPI_SURFACE_MEMORY(obj)           (GST_VAAPI_SURFACE_MEMORY_CAST(obj))

#define  GST_ALLOCATOR_SURFACE_MEMORY  "GstVaapiSurfaceMemoryAllocator"

typedef struct _GstVaapiSurfaceMemory GstVaapiSurfaceMemory;

struct _GstVaapiSurfaceMemory {
   GstMemory memory;
   GstVaapiSurface *surface;
   gsize slice_size;
   gpointer user_data;
   GDestroyNotify notify;
};

GstAllocator *
gst_vaapi_create_allocator (GstVaapiDisplay *display, GstCaps *caps);

GType
gst_vaapi_surface_memory_get_type (void);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_MEMORY_H */

