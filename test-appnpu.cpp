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

#include <png.h>
#include <iostream>
#include <fstream>

#define RENDERING_WIDTH 1280
#define RENDERING_HEIGHT 720
#define RENDERING_CHANNEL 4

#define RKNN_WIDTH 640
#define RKNN_HEIGHT 640
#define RKNN_CHANNEL 3

#define NMS_THRESH 0.45
#define BOX_THRESH 0.25

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

// --- Custom Data Structure ---
// A structure to hold all our GStreamer elements, making them accessible in callbacks.
typedef struct _CustomData {
    GstElement *pipeline;
    GstElement *source;         // either rtspsrc or filesrc
    GstElement *depay;          // only for rtsp
    GstElement *demuxer;        // only for local file
    GstElement *parse;
    GstElement *decoder;
    GstElement *videoscale;
    GstElement *scale_capsfilter;
    GstElement *videoconvert;
    GstElement *rgb_capsfilter;
    GstElement *sink;
    GMainLoop *main_loop;
} CustomData;

/**
 * @brief Callback function for dynamically linking pads.
 * This function can now handle both rtspsrc and qtdemux.
 */
static void on_pad_added(GstElement *src_element, GstPad *new_pad, CustomData *data) {
    g_print("Dynamic pad created, linking...\n");

    GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
    const gchar *new_pad_type = gst_structure_get_name(gst_caps_get_structure(new_pad_caps, 0));
    sleep(1);
    gchar *caps_str = gst_caps_to_string(new_pad_caps);
    g_print("Demuxer new pad caps: %s\n", caps_str);
    GstPad *sink_pad = gst_element_get_static_pad(data->parse, "sink");
    GstCaps *sink_caps = gst_pad_query_caps(sink_pad, NULL);
    gchar *sink_caps_str = gst_caps_to_string(sink_caps);
    g_print("h264parse sink pad caps: %s\n", sink_caps_str);
    g_free(caps_str);
    g_free(sink_caps_str);
    gst_caps_unref(sink_caps);

    // Link only if the media type is H264 video
    if (g_str_has_prefix(new_pad_type, "video/x-h264")) {
        g_print("Dynamic pad created, linking 264...\n");
        // Check which element the pad comes from, then get the sink pad of the next element in the chain
        const gchar *src_element_name = gst_element_get_name(src_element);
        if (g_strcmp0(src_element_name, "rtspsrc") == 0) {
            sink_pad = gst_element_get_static_pad(data->depay, "sink");
        } else if (g_strcmp0(src_element_name, "demuxer") == 0) {
            g_print("Dynamic pad created, linking demuxer...\n");
            sink_pad = gst_element_get_static_pad(data->parse, "sink");
        } else {
            g_warning("Pad added from unexpected element: %s", src_element_name);
            goto exit;
        }

        if (gst_pad_is_linked(sink_pad)) {
            g_print("Sink pad is already linked. Ignoring.\n");
            goto exit;
        }

        GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            g_warning("Failed to link dynamic pad.");
        } else {
            g_print("Dynamic pad linked successfully.\n");
        }
    }

exit:
    // Unreference the resources we created
    if (new_pad_caps != NULL) gst_caps_unref(new_pad_caps);
    if (sink_pad != NULL) gst_object_unref(sink_pad);
}

/**
 * @brief Callback function to handle messages from the bus (like errors, EOS).
 */
static gboolean on_bus_message(GstBus *bus, GstMessage *msg, CustomData *data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            g_main_loop_quit(data->main_loop);
            break;
        }
        case GST_MESSAGE_EOS:
            g_print("End-Of-Stream reached.\n");
            g_main_loop_quit(data->main_loop);
            break;
        default:
            // We are not interested in other messages
            break;
    }
    // Return TRUE to keep the bus watch active
    return TRUE;
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

void save_image_to_disk(const std::string &file_path, const guint8 *rgba_frame, int width, int height)
{
    FILE *fp = fopen(file_path.c_str(), "wb");
    if (!fp)
    {
        std::cerr << "Failed to open file for writing: " << file_path << std::endl;
        return;
    }

    // Create PNG write structure
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr)
    {
        std::cerr << "Failed to create PNG write structure." << std::endl;
        fclose(fp);
        return;
    }

    // Create PNG info structure
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        std::cerr << "Failed to create PNG info structure." << std::endl;
        png_destroy_write_struct(&png_ptr, nullptr);
        fclose(fp);
        return;
    }

    // Set error handling
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        std::cerr << "Error during PNG creation." << std::endl;
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    // Initialize PNG file I/O
    png_init_io(png_ptr, fp);

    // Write PNG header
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    // Write image data row by row
    for (int y = 0; y < height; y++)
    {
        png_bytep row = (png_bytep)(rgba_frame + y * width * 4); // 4 bytes per pixel (RGBA)
        png_write_row(png_ptr, row);
    }

    // End writing
    png_write_end(png_ptr, nullptr);

    // Clean up
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    std::cout << "Image saved to " << file_path << std::endl;
}

static GstPadProbeReturn process_frame_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    struct timeval start_time, stop_time;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    g_print("process_frame_callback ...\n");
    // Map the buffer to access frame data
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READWRITE))
    {
        // Assuming RGB format (video/x-raw, format=RGB)
        guint8 *rgba_frame = map.data; // Pointer to RGB data

        gettimeofday(&start_time, NULL);
        g_print("gst_buffer_map ...\n");
        // Resize the frame using RGA
        // rga_buffer_handle_t src_handle = importbuffer_virtualaddr(rgba_frame, RENDERING_WIDTH * RENDERING_HEIGHT * RENDERING_CHANNEL);
        // rga_buffer_handle_t dst_handle = importbuffer_virtualaddr(G_DIST_BUFFER, RKNN_WIDTH * RKNN_HEIGHT * RKNN_CHANNEL);
        // if (src_handle == 0 || dst_handle == 0)
        // {
        //     g_print("gst_buffer_map %d, dst_handle:%d...\n", src_handle, dst_handle);
        //     return GST_PAD_PROBE_OK;
        // }

         
        // rga_buffer_t src_img = wrapbuffer_handle(src_handle, RENDERING_WIDTH, RENDERING_HEIGHT, RK_FORMAT_BGRA_8888);
        // rga_buffer_t dst_img = wrapbuffer_handle(dst_handle, RKNN_WIDTH, RKNN_HEIGHT, RK_FORMAT_RGB_888);

        rga_buffer_t src_img = {0};
        rga_buffer_t dst_img = {0};
        src_img.vir_addr = (void *)rgba_frame; 
        src_img.width = RENDERING_WIDTH;
        src_img.height = RENDERING_HEIGHT;
        src_img.format = RK_FORMAT_RGBA_8888;
        src_img.wstride = RENDERING_WIDTH; 
        src_img.hstride = RENDERING_HEIGHT;

        dst_img.vir_addr = (void *)G_DIST_BUFFER;
        dst_img.width = RKNN_WIDTH;
        dst_img.height = RKNN_HEIGHT;
        dst_img.format = RK_FORMAT_RGB_888;
        dst_img.wstride = RKNN_WIDTH;
        dst_img.hstride = RKNN_HEIGHT;


        int ret = imcheck(src_img, dst_img, {}, {});
        if (ret != IM_STATUS_NOERROR)
        {
            return GST_PAD_PROBE_OK;
        }

        ret = imresize(src_img, dst_img);
        if (ret != IM_STATUS_SUCCESS)
        {
             g_print("imresize failed");
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
            draw_box_on_rgba_frame(rgba_frame, RENDERING_WIDTH, RENDERING_HEIGHT, det_result->box.left, det_result->box.top,
                                   det_result->box.right - det_result->box.left, det_result->box.bottom - det_result->box.top);

            // Draw text using FreeType
            draw_text_on_rgba_frame_freetype(rgba_frame, "./simsun.ttc", RENDERING_WIDTH, RENDERING_HEIGHT, det_result->name,
                                             det_result->box.left, det_result->box.top);
        }

        // save_image_to_disk("output.png", rgba_frame, RENDERING_WIDTH, RENDERING_HEIGHT);

        gettimeofday(&stop_time, NULL);
        std::cout << "Inference time: " << (__get_us(stop_time) - __get_us(start_time)) / 1000 << " ms" << std::endl;
        g_print("gst_buffer_map out...\n");
        rknn_outputs_release(G_RKNN_CONTEXT, G_IO_NUM.n_output, outputs);

        // Unmap when done
        gst_buffer_unmap(buffer, &map);
    }

    return GST_PAD_PROBE_OK;
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    CustomData data = {0}; // Initialize our data structure to zeros
    gchar *uri;

    // Your custom initialization
    bootstrap_init(&argc, &argv);

    // Initialize GStreamer
    gst_init(&argc, &argv);

   // Check for command-line arguments. If there are none, use a default URI.
    if (argc < 2) {
        uri = "/userdata/test/car.mp4";
        g_print("No URI provided. Using default: %s\n", uri);
    } else {
        uri = argv[1];
    }

    // --- 1. Create all potentially used elements ---
    // We will selectively use them based on the URI type
    data.pipeline = gst_pipeline_new("video-pipeline");

    // a. Common elements (used in both modes)
    data.parse = gst_element_factory_make("h264parse", "parse");
    data.decoder = gst_element_factory_make("mppvideodec", "decoder");
    data.videoscale = gst_element_factory_make("videoscale", "videoscale");
    data.scale_capsfilter = gst_element_factory_make("capsfilter", "scale_capsfilter");
    data.videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
    data.rgb_capsfilter = gst_element_factory_make("capsfilter", "rgb_capsfilter");
    data.sink = gst_element_factory_make("waylandsink", "sink");

    // --- 2. Determine URI type and build the data source ---
    if (g_str_has_prefix(uri, "rtsp://")) {
        g_print("Input is an RTSP stream. Building network pipeline...\n");
        // b. RTSP-specific elements
        data.source = gst_element_factory_make("rtspsrc", "rtspsrc");
        data.depay = gst_element_factory_make("rtph264depay", "depay");
        g_object_set(data.source, "location", uri, NULL);

    } else {
        g_print("Input is a local file. Building file pipeline...\n");
        // c. Local file-specific elements
        data.source = gst_element_factory_make("filesrc", "filesrc");
        data.demuxer = gst_element_factory_make("qtdemux", "demuxer");
        g_object_set(data.source, "location", uri, NULL);
    }

    // Check if all elements were created successfully
    if (!data.pipeline || !data.source || !data.parse || !data.decoder || !data.videoscale || !data.scale_capsfilter ||
        !data.videoconvert || !data.sink) {
        g_error("Failed to create one or more elements");
        return -1;
    }
    if ((!data.depay && g_str_has_prefix(uri, "rtsp://")) || (!data.demuxer && !g_str_has_prefix(uri, "rtsp://"))){
        g_error("Failed to create source-specific elements");
        return -1;
    }

    g_print("Input is a local file.configure caps...\n");
    // --- 3. Configure Caps and Properties ---
    GstCaps *scale_caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, RENDERING_WIDTH,
                                              "height", G_TYPE_INT, RENDERING_HEIGHT, NULL);
    g_object_set(data.scale_capsfilter, "caps", scale_caps, NULL);
    gst_caps_unref(scale_caps);

    GstCaps *rgb_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", NULL);
    g_object_set(data.rgb_capsfilter, "caps", rgb_caps, NULL);
    gst_caps_unref(rgb_caps);

    const gchar *property_name = "render-rectangle";
    GValue render_rectangle = G_VALUE_INIT;
    g_value_init(&render_rectangle, GST_TYPE_ARRAY);
    int coords[] = {0, 0, RENDERING_WIDTH, RENDERING_HEIGHT};
    for (int i = 0; i < 4; i++) {
        GValue val = G_VALUE_INIT;
        g_value_init(&val, G_TYPE_INT);
        g_value_set_int(&val, coords[i]);
        gst_value_array_append_value(&render_rectangle, &val);
        g_value_unset(&val);
    }
    g_object_set_property(G_OBJECT(data.sink), property_name, &render_rectangle);
    g_value_unset(&render_rectangle);

    // --- 4. Add and link the common elements ---
    gst_bin_add_many(GST_BIN(data.pipeline), data.parse, data.decoder, data.videoscale, data.scale_capsfilter,
                     data.videoconvert, data.rgb_capsfilter, data.sink, NULL);

    if (!gst_element_link_many(data.parse, data.decoder, data.videoscale, data.scale_capsfilter,
                               data.videoconvert, data.rgb_capsfilter, data.sink, NULL)) {
        g_error("Failed to link common elements");
        gst_object_unref(data.pipeline);
        return -1;
    }

    // --- 2. Determine URI type and build the data source ---
    if (g_str_has_prefix(uri, "rtsp://")) {
        g_print("Input is an RTSP stream. Building network pipeline...\n");
        gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.depay, NULL);
        gst_element_link(data.depay, data.parse);
        g_signal_connect(data.source, "pad-added", G_CALLBACK(on_pad_added), &data);

    } else {
        g_print("Input is a local file. Building file pipeline...\n");

        gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.demuxer, NULL);
        gst_element_link(data.source, data.demuxer);
        g_signal_connect(data.demuxer, "pad-added", G_CALLBACK(on_pad_added), &data);
    }

    // Add buffer probe for RGB processing on the rgb_capsfilter's src pad
    GstPad *rgb_capsfilter_src_pad = gst_element_get_static_pad(data.rgb_capsfilter, "src");
    gst_pad_add_probe(rgb_capsfilter_src_pad, GST_PAD_PROBE_TYPE_BUFFER, process_frame_callback, NULL, NULL);
    gst_object_unref(rgb_capsfilter_src_pad);
    // --- 5. Run the main loop ---
    GstBus *bus = gst_element_get_bus(data.pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)on_bus_message, &data);
    gst_object_unref(bus);

    gst_element_set_state(data.pipeline, GST_STATE_READY);
    g_print("Starting pipeline ready...\n");
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    data.main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(data.main_loop);

    // --- 6. Clean up resources ---
    g_print("Cleaning up...\n");
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    g_main_loop_unref(data.main_loop);

    return 0;
}
