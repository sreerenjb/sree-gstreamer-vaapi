/*
 *  gstvaapivideomemory - Gst VA video memory
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
 * SECTION:gstvaapivideomemory
 * @short_description: VA video memory
 */

#include "sysdeps.h"
#include "gstvaapivideomemory.h"
#define DEBUG 1
#include "gstvaapidebug.h"

static GstAllocator *_video_allocator;

G_DEFINE_TYPE (GstVaapiVideoAllocator, gst_vaapi_video_allocator, GST_TYPE_ALLOCATOR);

static GstMemory*
_gst_vaapi_video_mem_new (GstAllocator *allocator, GstMemory *parent,
    GstVaapiDisplay *display, GstVideoInfo *info)
{
    GstVaapiVideoMemory *mem;
    GstVaapiSurface *surface;

    GST_DEBUG ("alloc from allocator %p", allocator);
    mem = g_slice_new (GstVaapiVideoMemory);

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
    mem->raw_image   = g_slice_new (GstVaapiImageRaw);
    return (GstMemory *)mem;
}

static GstMemory*
_gst_vaapi_video_mem_alloc (GstAllocator *allocator, gsize size, GstAllocationParams * params)
{
     g_warning ("use gst_vaapi_video_memory_alloc () to allocate from this "
      "GstVaapiVideoMemory allocator");
     return NULL;
}

static void
_gst_vaapi_video_mem_free (GstAllocator *allocator, GstMemory *mem)
{
    GstVaapiVideoMemory *video_mem = (GstVaapiVideoMemory *)mem; 

    if (!video_mem)
	return;
    if (video_mem->surface) {
	g_object_unref(G_OBJECT(video_mem->surface));
        video_mem->surface = NULL;
    }
    if (video_mem->image) {
	g_object_unref(G_OBJECT(video_mem->image));
	video_mem->image = NULL;
    }
    if (video_mem->display) {
	g_object_unref(G_OBJECT(video_mem->display));
	video_mem->display = NULL;
    }
    if (video_mem->raw_image) {
	g_slice_free(GstVaapiImageRaw, video_mem->raw_image);
	video_mem->raw_image = NULL;
    }
    
    g_slice_free (GstVaapiVideoMemory, video_mem);
    GST_DEBUG ("%p: freed", video_mem);
}

static gboolean
ensure_data (GstVaapiVideoMemory * video_mem)
{
    GstMapInfo map_info;
    GstMemory *memory;
    GstVideoInfo *info;
    guint num_planes, i;
 
    info       = video_mem->info;
    
    if (!gst_vaapi_surface_get_image(video_mem->surface, video_mem->image))
    {
        GST_ERROR("Failed to get the image from surface");
	return FALSE;
    }
    
    if (!gst_vaapi_image_map_to_raw_image(video_mem->image, video_mem->raw_image)){
	GST_ERROR("Failed to map GstVaapiImage to GstVaapiImageRaw");	
	return FALSE;
    }	
    gst_vaapi_image_unmap(video_mem->image);
  
    video_mem->flag = GST_VAAPI_VIDEO_MEMORY_MAPPED;
    
    return TRUE;
}

static gpointer
_gst_vaapi_video_mem_map (GstVaapiVideoMemory *mem, gsize maxsize, GstMapFlags flags)
{
     GST_DEBUG ("surface:%d, maxsize:%d, flags:%d", mem->surface,
      maxsize, flags);
     if (!ensure_data (mem))
         return NULL;

     return mem->raw_image->pixels[0];
}

static void
_gst_vaapi_video_mem_unmap (GstVaapiVideoMemory *mem)
{
    GST_DEBUG("%p unmapped", mem->surface);
}

static GstMemory *
_gst_vaapi_video_mem_share (GstVaapiVideoMemory *mem, gssize offset, gsize size)
{
    /*Fixme*/
    return NULL;
}

static GstMemory *
_gst_vaapi_video_mem_copy (GstVaapiVideoMemory * src, gssize offset, gssize size)
{
    /*Fixme*/
    return NULL;
}

static gboolean
_gst_vaapi_video_mem_is_span (GstVaapiVideoMemory * mem1, GstVaapiVideoMemory * mem2,
    gsize * offset)
{
    /*Fixme*/
    return FALSE;
}

static void
gst_vaapi_video_allocator_class_init (GstVaapiVideoAllocatorClass * klass)
{
    GstAllocatorClass *allocator_class;

    allocator_class = (GstAllocatorClass *) klass;

    allocator_class->alloc = _gst_vaapi_video_mem_alloc;
    allocator_class->free = _gst_vaapi_video_mem_free;
}

static void
gst_vaapi_video_allocator_init (GstVaapiVideoAllocator * allocator)
{
    GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

    alloc->mem_type    = GST_VAAPI_VIDEO_MEMORY_NAME;
    alloc->mem_map     = (GstMemoryMapFunction) _gst_vaapi_video_mem_map;
    alloc->mem_unmap   = (GstMemoryUnmapFunction) _gst_vaapi_video_mem_unmap;
    alloc->mem_share   = (GstMemoryShareFunction) _gst_vaapi_video_mem_share;
    //alloc->mem_copy    = (GstMemoryCopyFunction) _gst_vaapi_video_mem_copy;
    //alloc->mem_is_span = (GstMemoryIsSpanFunction) _gst_vaapi_video_mem_is_span;
}

GstMemory *
gst_vaapi_video_memory_new (GstVaapiDisplay *display, GstVideoInfo *info)
{
    return (GstMemory *) _gst_vaapi_video_mem_new (_video_allocator, NULL, display,
      info);
}

void
gst_vaapi_video_memory_allocator_setup (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    _video_allocator =
        g_object_new (gst_vaapi_video_allocator_get_type (), NULL);

    gst_allocator_register (GST_VAAPI_VIDEO_ALLOCATOR_NAME, 
				gst_object_ref(_video_allocator));
    
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
  GstVaapiVideoMemory *vmem =
      (GstVaapiVideoMemory *) gst_buffer_get_memory (buffer, 0);

  /* Only handle GstVdpVideoMemory */
  g_return_val_if_fail (((GstMemory *) vmem)->allocator == _video_allocator,
      FALSE);
  GST_DEBUG ("plane:%d", plane);

  /* download if not already done */
  if (!ensure_data (vmem))
    return FALSE;
  
  *data = vmem->raw_image->pixels[plane];
  *stride = vmem->raw_image->stride[plane];

  return TRUE;
}

gboolean
gst_vaapi_video_memory_unmap (GstVideoMeta * meta, guint plane, GstMapInfo * info)
{
  GST_DEBUG ("plane:%d", plane);

  /*Fixme: implement*/
  return TRUE;
}

