/*
 *  gstvaapiimagememory.h - Gst VA image memory
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

#ifndef GST_VAAPI_IMAGE_MEMORY_H
#define GST_VAAPI_IMAGE_MEMORY_H

#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapivideopool.h>
#include <gst/video/gstvideometa.h>

G_BEGIN_DECLS

/** For internal use **/
typedef struct _ImageAllocatorParams ImageAllocatorParams;

struct _ImageAllocatorParams {
  GstVaapiDisplay *display;
  GstCaps *caps;
  GstVaapiImageFormat format;
  guint width;
  guint height;
};

#define  GST_VAAPI_IMAGE_MEMORY_NAME  "GstVaapiImageMemory"

/*------------- ImageAllocator -----------------*/

#define GST_VAAPI_TYPE_IMAGE_ALLOCATOR        (gst_vaapi_image_allocator_get_type())
#define GST_IS_VAAPI_IMAGE_ALLOCATOR(obj)     (GST_IS_OBJECT_TYPE(obj, GST_VAAPI_TYPE_IMAGE_ALLOCATOR))
#define GST_VAAPI_IMAGE_ALLOCATOR_CAST(obj)   ((GstVaapiImageAllocator *)(obj))
#define GST_VAAPI_IMAGE_ALLOCATOR(obj)        (GST_VAAPI_IMAGE_ALLOCATOR_CAST(obj))

#define  GST_VAAPI_IMAGE_ALLOCATOR_NAME  "GstVaapiImageAllocator"

typedef struct _GstVaapiImageAllocator GstVaapiImageAllocator;
typedef struct _GstVaapiImageAllocatorClass GstVaapiImageAllocatorClass;

struct _GstVaapiImageAllocator {
    GstAllocator parent;
    ImageAllocatorParams allocator_params;
};

struct _GstVaapiImageAllocatorClass
{
    GstAllocatorClass parent_class;
};

gboolean
gst_vaapi_image_memory_init(GstVaapiDisplay *display, GstCaps *caps);

GType
gst_vaapi_image_allocator_get_type (void);

G_END_DECLS

#endif /* GST_VAAPI_IMAGE_MEMORY_H */

