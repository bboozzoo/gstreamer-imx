/* IPU-based i.MX video sink class
 * Copyright (C) 2014  Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include "sink.h"
#include "blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_ipu_video_sink_debug);
#define GST_CAT_DEFAULT imx_ipu_video_sink_debug


enum
{
	PROP_0,
	PROP_OUTPUT_ROTATION,
	PROP_DEINTERLACE_MODE
};


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_IMX_IPU_BLITTER_SINK_CAPS
);


G_DEFINE_TYPE(GstImxIpuVideoSink, gst_imx_ipu_video_sink, GST_TYPE_IMX_BLITTER_VIDEO_SINK)


static void gst_imx_ipu_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_ipu_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_ipu_video_sink_start(GstImxBlitterVideoSink *blitter_video_sink);
static gboolean gst_imx_ipu_video_sink_stop(GstImxBlitterVideoSink *blitter_video_sink);

static void gst_imx_ipu_video_sink_set_transpose_flag(GstImxIpuVideoSink *ipu_video_sink);




/* required functions declared by G_DEFINE_TYPE */

void gst_imx_ipu_video_sink_class_init(GstImxIpuVideoSinkClass *klass)
{
	GObjectClass *object_class;
	GstImxBlitterVideoSinkClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_ipu_video_sink_debug, "imxipuvideosink", 0, "Freescale i.MX IPU video sink");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video sink",
		"Sink/Video",
		"Video output using the Freescale IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_get_property);

	base_class->start = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_start);
	base_class->stop  = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_stop);

	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Rotation that shall be applied to output frames",
			gst_imx_ipu_blitter_rotation_mode_get_type(),
			GST_IMX_IPU_BLITTER_OUTPUT_ROTATION_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_DEINTERLACE_MODE,
		g_param_spec_enum(
			"deinterlace-mode",
			"Deinterlace mode",
			"Deinterlacing mode to be used for incoming frames (ignored if frames are not interlaced)",
			gst_imx_ipu_blitter_deinterlace_mode_get_type(),
			GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_ipu_video_sink_init(GstImxIpuVideoSink *ipu_video_sink)
{
	ipu_video_sink->output_rotation = GST_IMX_IPU_BLITTER_OUTPUT_ROTATION_DEFAULT;
	ipu_video_sink->deinterlace_mode = GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT;
}



/* base class functions */

static void gst_imx_ipu_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_OUTPUT_ROTATION:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(ipu_video_sink);
			ipu_video_sink->output_rotation = g_value_get_enum(value);
			if (ipu_video_sink->blitter != NULL)
			{
				gst_imx_ipu_blitter_set_output_rotation_mode(ipu_video_sink->blitter, ipu_video_sink->output_rotation);
				gst_imx_ipu_video_sink_set_transpose_flag(ipu_video_sink);
			}
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(ipu_video_sink);
			break;

		case PROP_DEINTERLACE_MODE:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(ipu_video_sink);
			ipu_video_sink->deinterlace_mode = g_value_get_enum(value);
			if (ipu_video_sink->blitter != NULL)
				gst_imx_ipu_blitter_set_deinterlace_mode(ipu_video_sink->blitter, ipu_video_sink->deinterlace_mode);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(ipu_video_sink);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_ipu_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_OUTPUT_ROTATION:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(ipu_video_sink);
			g_value_set_enum(value, ipu_video_sink->output_rotation);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(ipu_video_sink);
			break;

		case PROP_DEINTERLACE_MODE:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(ipu_video_sink);
			g_value_set_enum(value, ipu_video_sink->deinterlace_mode);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(ipu_video_sink);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_ipu_video_sink_start(GstImxBlitterVideoSink *blitter_video_sink)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(blitter_video_sink);

	GstImxIpuBlitter *blitter = gst_imx_ipu_blitter_new();
	if (blitter == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not create IPU blitter");
		return FALSE;
	}

	gst_imx_ipu_blitter_set_output_rotation_mode(blitter, ipu_video_sink->output_rotation);
	gst_imx_ipu_blitter_set_deinterlace_mode(blitter, ipu_video_sink->deinterlace_mode);

	gst_imx_blitter_video_sink_set_blitter(blitter_video_sink, GST_IMX_BASE_BLITTER(blitter));
	gst_imx_ipu_video_sink_set_transpose_flag(ipu_video_sink);

	gst_object_unref(GST_OBJECT(blitter));

	/* no ref necessary, since the base class will clean up *after* any
	 * activity that might use the blitter has been shut down at that point */
	ipu_video_sink->blitter = blitter;

	return TRUE;
}


static gboolean gst_imx_ipu_video_sink_stop(G_GNUC_UNUSED GstImxBlitterVideoSink *blitter_video_sink)
{
	return TRUE;
}


static void gst_imx_ipu_video_sink_set_transpose_flag(GstImxIpuVideoSink *ipu_video_sink)
{
	switch (ipu_video_sink->output_rotation)
	{
		case GST_IMX_IPU_BLITTER_ROTATION_90CW:
		case GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP:
		case GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP:
		case GST_IMX_IPU_BLITTER_ROTATION_90CCW:
			gst_imx_blitter_video_sink_transpose_frames(GST_IMX_BLITTER_VIDEO_SINK(ipu_video_sink), TRUE);
			break;
		default:
			gst_imx_blitter_video_sink_transpose_frames(GST_IMX_BLITTER_VIDEO_SINK(ipu_video_sink), FALSE);
	}
}

