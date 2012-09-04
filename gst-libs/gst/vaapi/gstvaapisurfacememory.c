/*
 *  gstvaapisurfacememory - Gst VA surface memory
 *
 *  Copyright (C) 2011-2012 Intel Corporation
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

/**
 * SECTION:gstvaapisurfacememory
 * @short_description: VA surface memory
 */

#include "sysdeps.h"
#include "gstvaapisurfacememory.h"
#define DEBUG 1
#include "gstvaapidebug.h"

static GstAllocator *_surface_allocator;
static SurfaceAllocatorParams *_allocator_params;

typedef struct _GstVaapiSurfaceMemory GstVaapiSurfaceMemory;

struct _GstVaapiSurfaceMemory {
    GstMemory memory;
    GstVaapiSurface *surface;
    gsize slice_size;
    gpointer user_data;
    GDestroyNotify notify;
};

G_DEFINE_TYPE (GstVaapiSurfaceAllocator, gst_vaapi_surface_allocator, GST_TYPE_ALLOCATOR);

static GstVaapiSurface*
allocate_surface (GstAllocationParams *param, gsize maxsize, gsize size)
{
    GstVaapiSurface *surface;
    surface = gst_vaapi_surface_new(
                 _allocator_params->display,
                 GST_VAAPI_CHROMA_TYPE_YUV420,
                 _allocator_params->width, 
 	         _allocator_params->height
              );
    g_message ("initial surface creation 0X%X",surface);
    return surface;
}

static GstMemory*
_gst_vaapi_surface_mem_alloc (GstAllocator *allocator, gsize size,  GstAllocationParams * params)
{
    GstVaapiSurfaceMemory *mem;
    gsize maxsize;

    maxsize = size + params->prefix + params->padding;

    GST_DEBUG ("alloc from allocator %p", allocator);

    mem = g_slice_new (GstVaapiSurfaceMemory);

    gst_memory_init (GST_MEMORY_CAST (mem), params->flags, allocator, NULL,
      maxsize, params->align, params->prefix, size);

    mem->surface = (GstVaapiSurface *)allocate_surface (params, maxsize, size);

    return (GstMemory *)mem;
}

static void
_gst_vaapi_surface_mem_free (GstAllocator *allocator, GstMemory *mem)
{
    GstVaapiSurfaceMemory *surface_mem = (GstVaapiSurfaceMemory *)mem; 
    
    if (surface_mem->surface) {
	g_object_unref(G_OBJECT(surface_mem->surface));
        surface_mem->surface = NULL;
    }

    if (surface_mem->notify)
       surface_mem->notify (surface_mem->user_data);

    g_slice_free1 (surface_mem->slice_size, surface_mem);
    GST_DEBUG ("%p: freed", surface_mem);
}

static gpointer
_gst_vaapi_surface_mem_map (GstVaapiSurfaceMemory *mem, gsize maxsize, GstMapFlags flags)
{
    GST_DEBUG("%p mapped", mem->surface);
    return mem->surface;
}

static gboolean
_gst_vaapi_surface_mem_unmap (GstVaapiSurfaceMemory *mem)
{
    GST_DEBUG("%p unmapped", mem->surface);
    return TRUE;
}

static GstVaapiSurface*
_gst_vaapi_surface_mem_share (GstVaapiSurfaceMemory *mem, gssize offset, gsize size)
{
    return mem->surface;
}

static void
gst_vaapi_surface_allocator_class_init (GstVaapiSurfaceAllocatorClass * klass)
{
    GstAllocatorClass *allocator_class;

    allocator_class = (GstAllocatorClass *) klass;

    allocator_class->alloc = _gst_vaapi_surface_mem_alloc;
    allocator_class->free = _gst_vaapi_surface_mem_free;
}

static void
gst_vaapi_surface_allocator_init (GstVaapiSurfaceAllocator * allocator)
{
    GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

    alloc->mem_type = GST_VAAPI_SURFACE_MEMORY_NAME;
    alloc->mem_map = (GstMemoryMapFunction) _gst_vaapi_surface_mem_map;
    alloc->mem_unmap = (GstMemoryUnmapFunction) _gst_vaapi_surface_mem_unmap;
    alloc->mem_share = (GstMemoryShareFunction) _gst_vaapi_surface_mem_share;
}

gboolean
surface_allocator_params_init(
    GstVaapiSurfaceAllocator *surface_allocator, 
    GstVaapiDisplay *display, 
    GstCaps *caps)
{
    GstVideoInfo info;
    SurfaceAllocatorParams *allocator_params;
    /*Fixme*/
    allocator_params = &(surface_allocator->allocator_params);
    _allocator_params = &(surface_allocator->allocator_params);

    allocator_params->display = g_object_ref (G_OBJECT(display));
    allocator_params->caps    = gst_caps_ref (caps);
    if (!gst_video_info_from_caps (&info, caps))
    {
        GST_ERROR ("Failed to parse video info");
        return FALSE;
    }
    allocator_params->width  = info.width;
    allocator_params->height = info.height; 

    return TRUE;
}
gboolean
gst_vaapi_surface_memory_init (GstVaapiDisplay *display, GstCaps *caps)
{
    GstAllocator *allocator;
    GstVaapiSurfaceAllocator *surface_allocator;
    gboolean ret = TRUE;

    allocator = g_object_new (gst_vaapi_surface_allocator_get_type (), NULL);
    if (!allocator) {
	GST_ERROR("Failed to create the allocator");
	ret = FALSE;
	goto beach;
    }
    gst_allocator_register (GST_VAAPI_SURFACE_ALLOCATOR_NAME, allocator);

    _surface_allocator = allocator;

    surface_allocator = GST_VAAPI_SURFACE_ALLOCATOR(allocator);
    if (!surface_allocator_params_init(surface_allocator, display, caps)) {
        GST_ERROR("Failed to initialize the surface_allocator_params");
	ret = FALSE;
    }
beach:
    return ret;
}
