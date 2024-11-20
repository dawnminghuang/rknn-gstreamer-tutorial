#include <iostream>
#include <cstring> // For memset
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <gst/gst.h>
#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include <rga/RgaApi.h>
#include <rga/im2d.h>
#include <sys/mman.h>
#include <errno.h>

#include "rknn/rknn_api.h"
#include "yolov5/postprocess.h"

#define RENDERING_WIDTH 1280
#define RENDERING_HEIGHT 720
#define RENDERING_CHANNEL 4

#define RKNN_WIDTH 640
#define RKNN_HEIGHT 640
#define RKNN_CHANNEL 3

#define NMS_THRESH 0.45
#define BOX_THRESH 0.25

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

static void on_pad_added(GstElement *src, GstPad *new_pad, gpointer data)
{
    GstElement *depay = (GstElement *)data;

    // Get the sink pad of the next element in the pipeline (rtph264depay)
    GstPad *sink_pad = gst_element_get_static_pad(depay, "sink");

    // Check if the sink pad is already linked
    if (gst_pad_is_linked(sink_pad))
    {
        gst_object_unref(sink_pad);
        return;
    }

    // Attempt to link the new pad (source pad of rtspsrc) to the sink pad of depay
    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret))
    {
        std::cerr << "Link failed" << std::endl;
    }
    else
    {
        std::cout << "Link succeeded" << std::endl;
    }

    gst_object_unref(sink_pad);
}

// Function to draw a rectangle on an RGB frame
void draw_box_on_rgb_frame(guint8 *rgb_frame, int width, int height, int x, int y, int box_width, int box_height)
{
    // Each pixel in RGB is 3 bytes (R, G, B)
    for (int i = 0; i < box_width; i++)
    {
        // Draw top line of the box
        guint8 *top_pixel = rgb_frame + ((y * width + x + i) * 3);
        top_pixel[0] = 255; // R
        top_pixel[1] = 0;   // G
        top_pixel[2] = 0;   // B

        // Draw bottom line of the box
        guint8 *bottom_pixel = rgb_frame + (((y + box_height - 1) * width + x + i) * 3);
        bottom_pixel[0] = 255; // R
        bottom_pixel[1] = 0;   // G
        bottom_pixel[2] = 0;   // B
    }

    for (int i = 0; i < box_height; i++)
    {
        // Draw left line of the box
        guint8 *left_pixel = rgb_frame + (((y + i) * width + x) * 3);
        left_pixel[0] = 255; // R
        left_pixel[1] = 0;   // G
        left_pixel[2] = 0;   // B

        // Draw right line of the box
        guint8 *right_pixel = rgb_frame + (((y + i) * width + x + box_width - 1) * 3);
        right_pixel[0] = 255; // R
        right_pixel[1] = 0;   // G
        right_pixel[2] = 0;   // B
    }
}

// Function to draw a rectangle on an RGBA frame
void draw_box_on_rgba_frame(guint8 *rgba_frame, int width, int height, int x, int y, int box_width, int box_height)
{
    // Each pixel in RGBA is 4 bytes (R, G, B, A)
    for (int i = 0; i < box_width; i++)
    {
        // Draw top line of the box
        guint8 *top_pixel = rgba_frame + ((y * width + x + i) * 4);
        top_pixel[0] = 255; // R
        top_pixel[1] = 0;   // G
        top_pixel[2] = 0;   // B
        top_pixel[3] = 255; // A (opaque)

        // Draw bottom line of the box
        guint8 *bottom_pixel = rgba_frame + (((y + box_height - 1) * width + x + i) * 4);
        bottom_pixel[0] = 255; // R
        bottom_pixel[1] = 0;   // G
        bottom_pixel[2] = 0;   // B
        bottom_pixel[3] = 255; // A (opaque)
    }

    for (int i = 0; i < box_height; i++)
    {
        // Draw left line of the box
        guint8 *left_pixel = rgba_frame + (((y + i) * width + x) * 4);
        left_pixel[0] = 255; // R
        left_pixel[1] = 0;   // G
        left_pixel[2] = 0;   // B
        left_pixel[3] = 255; // A (opaque)

        // Draw right line of the box
        guint8 *right_pixel = rgba_frame + (((y + i) * width + x + box_width - 1) * 4);
        right_pixel[0] = 255; // R
        right_pixel[1] = 0;   // G
        right_pixel[2] = 0;   // B
        right_pixel[3] = 255; // A (opaque)
    }
}

FT_Library ft_library;
FT_Face ft_face;

// Function to draw text on RGB frame using FreeType
static void draw_text_on_rgb_frame_freetype(guint8 *rgb_frame, const char *font_path, guint width, guint height, const char *text, int x, int y)
{
    static int ft_init = -1;
    if (ft_init == -1)
    {
        if (FT_Init_FreeType(&ft_library))
        {
            return;
        }

        if (FT_New_Face(ft_library, font_path, 0, &ft_face))
        {
            FT_Done_FreeType(ft_library);
            return;
        }

        ft_init = 0;
    }

    // Set the font size
    FT_Set_Pixel_Sizes(ft_face, 0, 24);

    // Draw each character
    int pen_x = x;
    int pen_y = y;
    for (const char *p = text; *p; ++p)
    {
        if (FT_Load_Char(ft_face, *p, FT_LOAD_RENDER))
        {
            continue;
        }

        FT_Bitmap *bitmap = &ft_face->glyph->bitmap;
        int bitmap_width = bitmap->width;
        int bitmap_height = bitmap->rows;
        int bitmap_left = ft_face->glyph->bitmap_left;
        int bitmap_top = ft_face->glyph->bitmap_top;

        // Copy bitmap to RGB frame
        for (int row = 0; row < bitmap_height; ++row)
        {
            for (int col = 0; col < bitmap_width; ++col)
            {
                int x_offset = pen_x + bitmap_left + col;
                int y_offset = pen_y - bitmap_top + row;

                if (x_offset >= 0 && x_offset < width && y_offset >= 0 && y_offset < height)
                {
                    int index = (y_offset * width + x_offset) * 3;
                    uint8_t gray = bitmap->buffer[row * bitmap->width + col];
                    rgb_frame[index] = gray;     // Red
                    rgb_frame[index + 1] = gray; // Green
                    rgb_frame[index + 2] = gray; // Blue
                }
            }
        }

        pen_x += ft_face->glyph->advance.x >> 6; // Advance to the next character position
    }
}

// Function to draw text on RGBA frame using FreeType
static void draw_text_on_rgba_frame_freetype(guint8 *rgba_frame, const char *font_path, guint width, guint height, const char *text, int x, int y)
{
    static int ft_init = -1;
    if (ft_init == -1)
    {
        if (FT_Init_FreeType(&ft_library))
        {
            return;
        }

        if (FT_New_Face(ft_library, font_path, 0, &ft_face))
        {
            FT_Done_FreeType(ft_library);
            return;
        }

        ft_init = 0;
    }

    // Set the font size
    FT_Set_Pixel_Sizes(ft_face, 0, 24);

    // Draw each character
    int pen_x = x;
    int pen_y = y;
    for (const char *p = text; *p; ++p)
    {
        if (FT_Load_Char(ft_face, *p, FT_LOAD_RENDER))
        {
            continue;
        }

        FT_Bitmap *bitmap = &ft_face->glyph->bitmap;
        int bitmap_width = bitmap->width;
        int bitmap_height = bitmap->rows;
        int bitmap_left = ft_face->glyph->bitmap_left;
        int bitmap_top = ft_face->glyph->bitmap_top;

        // Copy bitmap to RGBA frame
        for (int row = 0; row < bitmap_height; ++row)
        {
            for (int col = 0; col < bitmap_width; ++col)
            {
                int x_offset = pen_x + bitmap_left + col;
                int y_offset = pen_y - bitmap_top + row;

                if (x_offset >= 0 && x_offset < width && y_offset >= 0 && y_offset < height)
                {
                    int index = (y_offset * width + x_offset) * 4;
                    uint8_t gray = bitmap->buffer[row * bitmap->width + col];

                    rgba_frame[index] = gray;     // Red
                    rgba_frame[index + 1] = gray; // Green
                    rgba_frame[index + 2] = gray; // Blue
                    rgba_frame[index + 3] = 255;  // Alpha (fully opaque)
                }
            }
        }

        pen_x += ft_face->glyph->advance.x >> 6; // Advance to the next character position
    }
}

void *G_DIST_BUFFER;
rknn_context G_RKNN_CONTEXT = 0;
rknn_input_output_num G_IO_NUM;
rknn_sdk_version G_SDK_VER;
rknn_tensor_attr *G_INPUT_ATTRS;
rknn_tensor_attr *G_OUTPUT_ATTRS;

static int bootstrap_init(int *argc, char ***argv)
{
    // Malloc a buffer for the output of the rknn model
    G_DIST_BUFFER = malloc(RKNN_WIDTH * RKNN_HEIGHT * 3);
    if (G_DIST_BUFFER == NULL)
    {
        return -1;
    }

    // Load RKNN Model
    int ret = rknn_init(&G_RKNN_CONTEXT, (void *)"./yolov5s-640-640.rknn", 0, 0, NULL);
    if (ret < 0)
    {
        std::cerr << "rknn_init fail! ret=" << ret << std::endl;
        return -1;
    }

    // Set core mask
    // ret = rknn_set_core_mask(G_RKNN_CONTEXT, RKNN_NPU_CORE_0_1_2);
    // if (ret < 0)
    // {
    //     std::cerr << "rknn_set_core_mask fail! ret=" << ret << std::endl;
    //     return -1;
    // }

    // Get sdk and driver version
    ret = rknn_query(G_RKNN_CONTEXT, RKNN_QUERY_SDK_VERSION, &G_SDK_VER, sizeof(G_SDK_VER));
    if (ret != RKNN_SUCC)
    {
        std::cerr << "rknn_query fail! ret=" << ret << std::endl;
        return -1;
    }
    std::cout << "api version: " << G_SDK_VER.api_version << std::endl;

    // Get Model Input Output Info
    ret = rknn_query(G_RKNN_CONTEXT, RKNN_QUERY_IN_OUT_NUM, &G_IO_NUM, sizeof(G_IO_NUM));
    if (ret != RKNN_SUCC)
    {
        std::cerr << "rknn_query fail! ret=" << ret << std::endl;
        return -1;
    }
    std::cout << "n_input=" << G_IO_NUM.n_input << " n_output=" << G_IO_NUM.n_output << std::endl;

    G_INPUT_ATTRS = (rknn_tensor_attr *)malloc(G_IO_NUM.n_input * sizeof(rknn_tensor_attr));
    memset(G_INPUT_ATTRS, 0, G_IO_NUM.n_input * sizeof(rknn_tensor_attr));
    for (uint32_t i = 0; i < G_IO_NUM.n_input; i++)
    {
        G_INPUT_ATTRS[i].index = i;
        rknn_query(G_RKNN_CONTEXT, RKNN_QUERY_INPUT_ATTR, &(G_INPUT_ATTRS[i]), sizeof(rknn_tensor_attr));
    }
    std::cout << "input[0] fmt=" << get_format_string(G_INPUT_ATTRS[0].fmt) << std::endl; // npu only support NHWC in zero copy mode

    G_OUTPUT_ATTRS = (rknn_tensor_attr *)malloc(G_IO_NUM.n_output * sizeof(rknn_tensor_attr));
    memset(G_OUTPUT_ATTRS, 0, G_IO_NUM.n_output * sizeof(rknn_tensor_attr));
    for (int i = 0; i < G_IO_NUM.n_output; i++)
    {
        G_OUTPUT_ATTRS[i].index = i;
        rknn_query(G_RKNN_CONTEXT, RKNN_QUERY_OUTPUT_ATTR, &(G_OUTPUT_ATTRS[i]), sizeof(rknn_tensor_attr));
    }
    std::cout << "output[0] fmt=" << get_format_string(G_OUTPUT_ATTRS[0].fmt) << std::endl;

    return 0;
}

static GstPadProbeReturn process_frame_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    struct timeval start_time, stop_time;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    // Map the buffer to access frame data
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READWRITE))
    {
        // Assuming RGB format (video/x-raw, format=RGB)
        guint8 *rgb_frame = map.data; // Pointer to RGB data

        gettimeofday(&start_time, NULL);

        // Resize the frame using RGA
        rga_buffer_handle_t src_handle = importbuffer_virtualaddr(rgb_frame, RENDERING_WIDTH * RENDERING_HEIGHT * RENDERING_CHANNEL);
        rga_buffer_handle_t dst_handle = importbuffer_virtualaddr(G_DIST_BUFFER, RKNN_WIDTH * RKNN_HEIGHT * RKNN_CHANNEL);
        if (src_handle == 0 || dst_handle == 0)
        {
            return GST_PAD_PROBE_OK;
        }

        rga_buffer_t src_img = wrapbuffer_handle(src_handle, RENDERING_WIDTH, RENDERING_HEIGHT, RK_FORMAT_RGBA_8888);
        rga_buffer_t dst_img = wrapbuffer_handle(dst_handle, RKNN_WIDTH, RKNN_HEIGHT, RK_FORMAT_RGB_888);

        int ret = imcheck(src_img, dst_img, {}, {});
        if (ret != IM_STATUS_NOERROR)
        {
            return GST_PAD_PROBE_OK;
        }

        ret = imresize(src_img, dst_img);
        if (ret != IM_STATUS_SUCCESS)
        {
            std::cerr << "imresize failed" << std::endl;
            return GST_PAD_PROBE_OK;
        }

        float scale_w = (float)RKNN_WIDTH / RENDERING_WIDTH;
        float scale_h = (float)RKNN_HEIGHT / RENDERING_HEIGHT;

        rknn_input inputs[1];
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index = 0;
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = RKNN_WIDTH * RKNN_HEIGHT * RKNN_CHANNEL;
        inputs[0].fmt = RKNN_TENSOR_NHWC;
        inputs[0].pass_through = 0;
        inputs[0].buf = G_DIST_BUFFER;

        rknn_inputs_set(G_RKNN_CONTEXT, G_IO_NUM.n_input, inputs);

        rknn_output outputs[G_IO_NUM.n_output];
        memset(outputs, 0, sizeof(outputs));
        for (int i = 0; i < G_IO_NUM.n_output; i++)
        {
            outputs[i].index = i;
            outputs[i].want_float = 0;
        }

        ret = rknn_run(G_RKNN_CONTEXT, NULL);
        ret = rknn_outputs_get(G_RKNN_CONTEXT, G_IO_NUM.n_output, outputs, NULL);

        detect_result_group_t detect_result_group;
        std::vector<float> out_scales;
        std::vector<int32_t> out_zps;
        for (int i = 0; i < G_IO_NUM.n_output; ++i)
        {
            out_scales.push_back(G_OUTPUT_ATTRS[i].scale);
            out_zps.push_back(G_OUTPUT_ATTRS[i].zp);
        }
        post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, RKNN_HEIGHT, RKNN_WIDTH,
                     BOX_THRESH, NMS_THRESH, scale_w, scale_h, out_zps, out_scales, &detect_result_group);

        for (int i = 0; i < detect_result_group.count; i++)
        {
            detect_result_t *det_result = &(detect_result_group.results[i]);

            // Draw a box on the RGB frame
            draw_box_on_rgba_frame(rgb_frame, RENDERING_WIDTH, RENDERING_HEIGHT, det_result->box.left, det_result->box.top,
                                   det_result->box.right - det_result->box.left, det_result->box.bottom - det_result->box.top);

            // Draw text using FreeType
            draw_text_on_rgba_frame_freetype(rgb_frame, "./simsun.ttc", RENDERING_WIDTH, RENDERING_HEIGHT, det_result->name,
                                             det_result->box.left, det_result->box.top);
        }

        gettimeofday(&stop_time, NULL);
        std::cout << "Inference time: " << (__get_us(stop_time) - __get_us(start_time)) / 1000 << " ms" << std::endl;

        rknn_outputs_release(G_RKNN_CONTEXT, G_IO_NUM.n_output, outputs);

        // Unmap when done
        gst_buffer_unmap(buffer, &map);
    }

    return GST_PAD_PROBE_OK;
}

int main(int argc, char *argv[])
{
    bootstrap_init(&argc, &argv);

    gst_init(&argc, &argv);

    // Create GStreamer pipeline
    GstElement *pipeline = gst_pipeline_new("video-pipeline");

    // Create elements
    GstElement *rtspsrc = gst_element_factory_make("rtspsrc", "rtspsrc");
    GstElement *depay = gst_element_factory_make("rtph264depay", "depay");
    GstElement *parse = gst_element_factory_make("h264parse", "parse");
    GstElement *decoder = gst_element_factory_make("mppvideodec", "decoder");

    GstElement *videoscale = gst_element_factory_make("videoscale", "videoscale");
    GstElement *scale_capsfilter = gst_element_factory_make("capsfilter", "scale_capsfilter");

    GstElement *videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
    GstElement *rgb_capsfilter = gst_element_factory_make("capsfilter", "rgb_capsfilter");

    const gchar *property_name = "render-rectangle";
    GValue render_rectangle = G_VALUE_INIT;
    g_value_init(&render_rectangle, GST_TYPE_ARRAY);
    int coords[] = {0, 0, RENDERING_WIDTH, RENDERING_HEIGHT};
    for (int i = 0; i < 4; i++)
    {
        GValue val = G_VALUE_INIT;
        g_value_init(&val, G_TYPE_INT);
        g_value_set_int(&val, coords[i]);
        gst_value_array_append_value(&render_rectangle, &val);
    }
    GstElement *sink = gst_element_factory_make_with_properties("waylandsink", 1, &property_name, &render_rectangle);

    if (!pipeline || !rtspsrc || !depay || !parse || !decoder || !videoscale || !scale_capsfilter || !videoconvert || !rgb_capsfilter || !sink)
    {
        g_error("Failed to create elements");
        return -1;
    }

    // Set RTSP location
    g_object_set(G_OBJECT(rtspsrc), "location", "rtspt://admin:admin123@192.168.44.138", NULL);

    // Create caps for scaling and filtering
    GstCaps *scale_caps = gst_caps_new_simple("video/x-raw",
                                              "width", G_TYPE_INT, RENDERING_WIDTH,
                                              "height", G_TYPE_INT, RENDERING_HEIGHT,
                                              "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                                              NULL);
    g_object_set(G_OBJECT(scale_capsfilter), "caps", scale_caps, NULL);
    gst_caps_unref(scale_caps);

    // Create caps for RGB format
    GstCaps *rgb_caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "RGBA",
                                            NULL);
    g_object_set(G_OBJECT(rgb_capsfilter), "caps", rgb_caps, NULL);
    gst_caps_unref(rgb_caps);

    // Add elements to the pipeline
    gst_bin_add_many(GST_BIN(pipeline), rtspsrc, depay, parse, decoder, videoscale, scale_capsfilter, videoconvert, rgb_capsfilter, sink, NULL);

    // Link elements (except for rtspsrc)
    gst_element_link_many(depay, parse, decoder, videoscale, scale_capsfilter, videoconvert, rgb_capsfilter, sink, NULL);

    // Connect the dynamic pad for rtspsrc
    g_signal_connect(rtspsrc, "pad-added", G_CALLBACK(on_pad_added), depay);

    // Add buffer probe for RGB processing on the rgb_capsfilter's src pad
    GstPad *rgb_capsfilter_src_pad = gst_element_get_static_pad(rgb_capsfilter, "src");
    gst_pad_add_probe(rgb_capsfilter_src_pad, GST_PAD_PROBE_TYPE_BUFFER, process_frame_callback, NULL, NULL);
    gst_object_unref(rgb_capsfilter_src_pad);

    // Start playing the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Run the main loop
    GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);

    // Clean up
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(main_loop);

    return 0;
}