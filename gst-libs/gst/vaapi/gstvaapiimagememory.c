/*
 *  gstvaapiimagememory - Gst VA image memory
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
 * SECTION:gstvaapiimagememory
 * @short_description: VA image memory
 */

#include "sysdeps.h"
#include "gstvaapiimagememory.h"
#define DEBUG 1
#include "gstvaapidebug.h"

static GstAllocator *_image_allocator;
static ImageAllocatorParams *_allocator_params;

typedef struct _GstVaapiImageMemory GstVaapiImageMemory;

struct _GstVaapiImageMemory {
    GstMemory memory;
    GstVaapiImage *image;
    gsize slice_size;
    gpointer user_data;
    GDestroyNotify notify;
};

G_DEFINE_TYPE (GstVaapiImageAllocator, gst_vaapi_image_allocator, GST_TYPE_ALLOCATOR);

static GstVaapiImage*
allocate_image (GstAllocationParams *param, gsize maxsize, gsize size)
{
    GstVaapiImage *image;
    image = gst_vaapi_image_new(
                 _allocator_params->display,
                 _allocator_params->format,
                 _allocator_params->width, 
 	         _allocator_params->height
              );
    return image;
}

static GstMemory*
_gst_vaapi_image_mem_alloc (GstAllocator *allocator, gsize size,  GstAllocationParams * params)
{
    GstVaapiImageMemory *mem;
    gsize maxsize;

    maxsize = size + params->prefix + params->padding;

    GST_DEBUG ("alloc from allocator %p", allocator);

    mem = g_slice_new (GstVaapiImageMemory);

    gst_memory_init (GST_MEMORY_CAST (mem), params->flags, allocator, NULL,
      maxsize, params->align, params->prefix, size);

    mem->image = (GstVaapiImage *)allocate_image (params, maxsize, size);

    return (GstMemory *)mem;
}

static void
_gst_vaapi_image_mem_free (GstAllocator *allocator, GstMemory *mem)
{
    GstVaapiImageMemory *image_mem = (GstVaapiImageMemory *)mem; 
    
    if (image_mem->image) {
	g_object_unref(G_OBJECT(image_mem->image));
        image_mem->image = NULL;
    }

    if (image_mem->notify)
       image_mem->notify (image_mem->user_data);

    g_slice_free1 (image_mem->slice_size, image_mem);
    GST_DEBUG ("%p: freed", image_mem);
}

static gpointer
_gst_vaapi_image_mem_map (GstVaapiImageMemory *mem, gsize maxsize, GstMapFlags flags)
{
    GST_DEBUG("%p mapped", mem->image);
    return mem->image;
}

static gboolean
_gst_vaapi_image_mem_unmap (GstVaapiImageMemory *mem)
{
    GST_DEBUG("%p unmapped", mem->image);
    return TRUE;
}

static GstVaapiImage*
_gst_vaapi_image_mem_share (GstVaapiImageMemory *mem, gssize offset, gsize size)
{
    return mem->image;
}

static void
gst_vaapi_image_allocator_class_init (GstVaapiImageAllocatorClass * klass)
{
    GstAllocatorClass *allocator_class;

    allocator_class = (GstAllocatorClass *) klass;

    allocator_class->alloc = _gst_vaapi_image_mem_alloc;
    allocator_class->free = _gst_vaapi_image_mem_free;
}

static void
gst_vaapi_image_allocator_init (GstVaapiImageAllocator * allocator)
{
    GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

    alloc->mem_type = GST_VAAPI_IMAGE_MEMORY_NAME;
    alloc->mem_map = (GstMemoryMapFunction) _gst_vaapi_image_mem_map;
    alloc->mem_unmap = (GstMemoryUnmapFunction) _gst_vaapi_image_mem_unmap;
    alloc->mem_share = (GstMemoryShareFunction) _gst_vaapi_image_mem_share;
}

gboolean
image_allocator_params_init(
    GstVaapiImageAllocator *image_allocator, 
    GstVaapiDisplay *display, 
    GstCaps *caps)
{
    GstVideoInfo info;
    ImageAllocatorParams *allocator_params;
    /*Fixme*/
    allocator_params = &(image_allocator->allocator_params);
    _allocator_params = &(image_allocator->allocator_params);

    allocator_params->display = g_object_ref (G_OBJECT(display));
    allocator_params->caps    = gst_caps_ref (caps);
    if (!gst_video_info_from_caps (&info, caps))
    {
        GST_ERROR ("Failed to parse video info");
        return FALSE;
    }
    allocator_params->width  = info.width;
    allocator_params->height = info.height; 

    allocator_params->format = gst_vaapi_image_format_from_caps(caps);
    return TRUE;
}
gboolean
gst_vaapi_image_memory_init (GstVaapiDisplay *display, GstCaps *caps)
{
    GstAllocator *allocator;
    GstVaapiImageAllocator *image_allocator;
    gboolean ret = TRUE;

    allocator = g_object_new (gst_vaapi_image_allocator_get_type (), NULL);
    if (!allocator) {
	GST_ERROR("Failed to create the allocator");
	ret = FALSE;
	goto beach;
    }
    gst_allocator_register (GST_VAAPI_IMAGE_ALLOCATOR_NAME, allocator);

    _image_allocator = allocator;

    image_allocator = GST_VAAPI_IMAGE_ALLOCATOR(allocator);
    if (!image_allocator_params_init(image_allocator, display, caps)) {
        GST_ERROR("Failed to initialize the image_allocator_params");
	ret = FALSE;
    }
beach:
    return ret;
}
