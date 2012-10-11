/*
 *  gstvaapisurfacememory - Gst VA surface memory
 *
 *  Copyright (C) 2011-2012 Intel Corporation
 *  Copyright (C) : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

G_DEFINE_TYPE (GstVaapiSurfaceAllocator, gst_vaapi_surface_allocator, GST_TYPE_ALLOCATOR);

static GstMemory*
_gst_vaapi_surface_mem_new (GstAllocator *allocator, GstMemory *parent,
    GstVaapiDisplay *display, GstVideoInfo *info)
{
    GstVaapiSurfaceMemory *mem;
    GstVaapiSurface *surface;

    GST_DEBUG ("alloc from allocator %p", allocator);
    mem = g_slice_new (GstVaapiSurfaceMemory);

    gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_READONLY , allocator, parent,
      GST_VIDEO_INFO_SIZE(info), 0, 0, GST_VIDEO_INFO_SIZE(info));

    surface = gst_vaapi_surface_new(
                      display,
                      GST_VAAPI_CHROMA_TYPE_YUV420,
                      info->width, 
 	              info->height
                  );
    if (!surface) {
	GST_ERROR("Failed to create GstVaapiSurface");
	return NULL;
    }
    mem->surface     = surface;
    mem->info        = info;
    mem->display     = g_object_ref(G_OBJECT(display));
    mem->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    mem->image       = gst_vaapi_image_new (mem->display, GST_VAAPI_IMAGE_NV12, 
			  info->width,info->height);
    mem->cache 	     = g_malloc (GST_VIDEO_INFO_SIZE (info));
    return (GstMemory *)mem;
}

static GstMemory*
_gst_vaapi_surface_mem_alloc (GstAllocator *allocator, gsize size, GstAllocationParams * params)
{
     g_warning ("use gst_vaapi_surface_memory_alloc () to allocate from this "
      "GstVaapiSurfaceMemory allocator");
     return NULL;
}

static void
_gst_vaapi_surface_mem_free (GstAllocator *allocator, GstMemory *mem)
{
    GstVaapiSurfaceMemory *surface_mem = (GstVaapiSurfaceMemory *)mem; 

    if (!surface_mem)
	return;
    if (surface_mem->surface) {
	g_object_unref(G_OBJECT(surface_mem->surface));
        surface_mem->surface = NULL;
    }

    if (surface_mem->display) {
	g_object_unref(G_OBJECT(surface_mem->display));
	surface_mem->display = NULL;
    }
    if (surface_mem->cache) {
	g_free(surface_mem->cache);
	surface_mem->cache = NULL;
    }
    
    g_slice_free (GstVaapiSurfaceMemory, surface_mem);
    GST_DEBUG ("%p: freed", surface_mem);
}

static gboolean
ensure_data (GstVaapiSurfaceMemory * surface_mem)
{
    GstMapInfo map_info;
    GstMemory *memory;
    GstVideoInfo *info;
    GstVaapiImageRaw raw_image;
    guint num_planes, i;
 
    info       = surface_mem->info;
    num_planes = GST_VIDEO_INFO_N_PLANES(info);
    
    for (i=0; i<num_planes; i++) {
        surface_mem->cached_data[i] = surface_mem->cache + GST_VIDEO_INFO_PLANE_OFFSET (info, i);
        surface_mem->destination_pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (info, i);
        
        GST_DEBUG ("cached_data %p ", surface_mem->cached_data[i]);
        GST_DEBUG ("pitches %d ", surface_mem->destination_pitches[i]);
    
	raw_image.pixels[i] = surface_mem->cached_data[i];
    	raw_image.stride[i] = surface_mem->destination_pitches[i];
    }
    /*
    gst_vaapi_image_map(surface_mem->image);
    surface_mem->cache = gst_vaapi_image_get_plane(surface_mem->image, 0);
    gst_vaapi_image_unmap(surface_mem->image);
    */    
    if (!gst_vaapi_surface_get_image(surface_mem->surface, surface_mem->image))
    {
        GST_ERROR("Failed to get the image from surface");
	return FALSE;
    }
    
    gst_vaapi_image_map(surface_mem->image);
    surface_mem->cache = gst_vaapi_image_get_plane(surface_mem->image, 0);
    gst_vaapi_image_unmap(surface_mem->image);
    surface_mem->flag = GST_VAAPI_SURFACE_MEMORY_MAPPED;
    /* 
    raw_image.format = GST_VAAPI_IMAGE_NV12;
    raw_image.width  = surface_mem->info->width;
    raw_image.height = surface_mem->info->height;
    
    if (!gst_vaapi_image_get_raw(surface_mem->image, &raw_image, NULL)) {
        GST_ERROR("Failed to get raw image");
	return FALSE;
    }*/
    return TRUE;
}

static gpointer
_gst_vaapi_surface_mem_map (GstVaapiSurfaceMemory *mem, gsize maxsize, GstMapFlags flags)
{
     GST_DEBUG ("surface:%d, maxsize:%d, flags:%d", mem->surface,
      maxsize, flags);
     if (!ensure_data (mem))
         return NULL;

     return mem->cache;
}

static void
_gst_vaapi_surface_mem_unmap (GstVaapiSurfaceMemory *mem)
{
    GST_DEBUG("%p unmapped", mem->surface);
}

static GstMemory *
_gst_vaapi_surface_mem_share (GstVaapiSurfaceMemory *mem, gssize offset, gsize size)
{
    /*Fixme*/
    return NULL;
}

static GstMemory *
_gst_vaapi_surface_mem_copy (GstVaapiSurfaceMemory * src, gssize offset, gssize size)
{
    /*Fixme*/
    return NULL;
}

static gboolean
_gst_vaapi_surface_mem_is_span (GstVaapiSurfaceMemory * mem1, GstVaapiSurfaceMemory * mem2,
    gsize * offset)
{
    /*Fixme*/
    return FALSE;
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

    alloc->mem_type    = GST_VAAPI_SURFACE_MEMORY_NAME;
    alloc->mem_map     = (GstMemoryMapFunction) _gst_vaapi_surface_mem_map;
    alloc->mem_unmap   = (GstMemoryUnmapFunction) _gst_vaapi_surface_mem_unmap;
    alloc->mem_share   = (GstMemoryShareFunction) _gst_vaapi_surface_mem_share;
    //alloc->mem_copy    = (GstMemoryCopyFunction) _gst_vaapi_surface_mem_copy;
    //alloc->mem_is_span = (GstMemoryIsSpanFunction) _gst_vaapi_surface_mem_is_span;
}

GstMemory *
gst_vaapi_surface_memory_new (GstVaapiDisplay *display, GstVideoInfo *info)
{
    return (GstMemory *) _gst_vaapi_surface_mem_new (_surface_allocator, NULL, display,
      info);
}

void
gst_vaapi_surface_memory_allocator_setup (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    _surface_allocator =
        g_object_new (gst_vaapi_surface_allocator_get_type (), NULL);

    gst_allocator_register (GST_VAAPI_SURFACE_ALLOCATOR_NAME, 
				gst_object_ref(_surface_allocator));
    
    GST_DEBUG("VA Surface Memory allocator has been successfully initialized...!"); 
    g_once_init_leave (&_init, 1);
  }
}

/*just copied from vdpau port*/
gboolean
gst_vaapi_video_memory_map (GstVideoMeta * meta, guint plane, GstMapInfo * info,
    gpointer * data, gint * stride, GstMapFlags flags)
{
  GstBuffer *buffer = meta->buffer;
  GstVaapiSurfaceMemory *vmem =
      (GstVaapiSurfaceMemory *) gst_buffer_get_memory (buffer, 0);

  /* Only handle GstVdpVideoMemory */
  g_return_val_if_fail (((GstMemory *) vmem)->allocator == _surface_allocator,
      FALSE);

  GST_DEBUG ("plane:%d", plane);

  /* download if not already done */
  if (!ensure_data (vmem))
    return FALSE;

  *data = vmem->cached_data[plane];
  *stride = vmem->destination_pitches[plane];

  return TRUE;
}

gboolean
gst_vaapi_video_memory_unmap (GstVideoMeta * meta, guint plane, GstMapInfo * info)
{
  GST_DEBUG ("plane:%d", plane);

  /*Fixme: implement*/
  return TRUE;
}

