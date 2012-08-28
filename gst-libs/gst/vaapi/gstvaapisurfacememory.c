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

GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiSurfaceMemory, gst_vaapi_surface_memory);

typedef struct _AllocatorParams AllocatorParams;

struct _AllocatorParams {
  GstVaapiDisplay *display;
  GstCaps *caps;
  guint width;
  guint height;
};

static AllocatorParams *_allocator_params;

static gpointer
gst_vaapi_surface_mem_map (GstVaapiSurfaceMemory *mem, gsize maxsize, GstMapFlags flags)
{
  return mem->surface;
}

static gboolean
gst_vaapi_surface_mem_unmap (GstVaapiSurfaceMemory *mem)
{
  return TRUE;
}

static void
gst_vaapi_surface_mem_free (GstVaapiSurfaceMemory *mem)
{
  
 
  if (mem->surface)
    mem->surface = NULL;

  if (mem->memory.parent)
    gst_memory_unref (mem->memory.parent);

  if (mem->notify)
    mem->notify (mem->user_data);

   g_slice_free1 (mem->slice_size, mem);
}

static GstVaapiSurfaceMemory*
gst_vaapi_surface_mem_share (GstVaapiSurfaceMemory *mem, gssize offset, gsize size)
{
  return (GstVaapiSurfaceMemory *)gst_mini_object_ref(GST_MINI_OBJECT_CAST(mem));
}

static void
_vaapi_surface_memory_allocator_notify (gpointer user_data)
{
  g_warning ("Vaapi Surface Memory Allocator was freed..!");
  if (user_data)
    g_slice_free1 ((AllocatorParams *) user_data,sizeof(AllocatorParams));
}

static void
default_mem_init (GstVaapiSurfaceMemory * mem, GstMemoryFlags flags,
    GstMemory * parent, gsize maxsize, gsize offset, gsize size, gsize align, gsize slice_size)
{
   mem->memory.parent = parent ? gst_memory_ref (parent) : NULL;
   mem->memory.state = (flags & GST_MEMORY_FLAG_READONLY ? 0x1 : 0);
   mem->memory.maxsize = maxsize;
   mem->memory.align = align;
   mem->memory.offset = offset;
   mem->memory.size = size;
   mem->slice_size = slice_size;
}

static void
gst_vaapi_surface_memory_init (GstVaapiSurfaceMemory *surface_mem, gsize slice_size)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (surface_mem), GST_VAAPI_TYPE_SURFACE_MEMORY);
  surface_mem->slice_size = slice_size;
}

static GstVaapiSurfaceMemory *
gst_vaapi_surface_memory_new (void)
{
  GstVaapiSurfaceMemory *surface_mem;
  gsize  slice_size;
  slice_size = sizeof (GstVaapiSurfaceMemory);

  surface_mem = g_slice_alloc (slice_size);
  if (surface_mem == NULL)
  {
        GST_ERROR ("Failed to allcoate GstVaapiSurfaceMemory....");
        return NULL;
  }

  gst_vaapi_surface_memory_init (surface_mem, slice_size);
  return GST_VAAPI_SURFACE_MEMORY_CAST(surface_mem);
}

static GstVaapiSurfaceMemory*
vaapi_surface_new_mem (gpointer user_data, GstMemoryFlags flags, gsize maxsize, gsize align, gsize offset, gsize size)
{
  GstVaapiSurfaceMemory *surface_mem;

  surface_mem = gst_vaapi_surface_memory_new ();

  default_mem_init (surface_mem, flags, NULL, maxsize,
      offset, size, align, sizeof(GstVaapiSurfaceMemory));

  surface_mem->surface = gst_vaapi_surface_new(
            _allocator_params->display,
            GST_VAAPI_CHROMA_TYPE_YUV420,
            _allocator_params->width, _allocator_params->height
        );
  return surface_mem;

}

static GstMemory*
gst_vaapi_surface_mem_alloc_alloc (GstAllocator *allocator, gsize size,  GstAllocationParams * params, gpointer user_data)
{
  GstMemory *mem;
  gsize maxsize;

  maxsize = size + params->prefix + params->padding;

  mem =  (GstMemory *)vaapi_surface_new_mem  (user_data, params->flags, maxsize, params->align, params->prefix, size);
  mem->allocator = allocator;
  mem->offset = 0;
  return mem;
}

GstAllocator *
gst_vaapi_create_allocator (GstVaapiDisplay *display, GstCaps *caps)
{
    GstAllocator *allocator;
    GstVideoInfo info;

    _allocator_params = g_slice_alloc (sizeof (AllocatorParams));
    _allocator_params->display = g_object_ref (G_OBJECT(display));
    _allocator_params->caps    = gst_caps_ref (caps);
    if (!gst_video_info_from_caps (&info, caps))
    {
        GST_ERROR ("Failed to parse video info");
        return;
    }
    _allocator_params->width  = info.width;
    _allocator_params->height = info.height;
    
    static const GstMemoryInfo surface_mem_info = {
      GST_ALLOCATOR_SURFACE_MEMORY,
      (GstAllocatorAllocFunction) gst_vaapi_surface_mem_alloc_alloc,
      (GstMemoryMapFunction) gst_vaapi_surface_mem_map,
      (GstMemoryUnmapFunction) gst_vaapi_surface_mem_unmap,
      (GstMemoryFreeFunction) gst_vaapi_surface_mem_free,
      (GstMemoryCopyFunction)  NULL,
      (GstMemoryShareFunction) gst_vaapi_surface_mem_share,
      (GstMemoryIsSpanFunction) NULL,
    };

    allocator = gst_allocator_new (&surface_mem_info, _allocator_params, _vaapi_surface_memory_allocator_notify);
    gst_allocator_register (GST_ALLOCATOR_SURFACE_MEMORY, allocator);

    return allocator;
}

