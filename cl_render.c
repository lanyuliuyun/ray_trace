
#include "common.h"

#include <CL/cl.h>

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

extern uint64_t now_ms(void);

struct opencl_global
{
    cl_platform_id opencl_platform;
    cl_device_id opencl_device;
    cl_context opencl_device_context;
    cl_command_queue command_queue;

    cl_program program;
    cl_kernel render_gradient_kernel;
    cl_kernel render_project_depth_kernel;
    
    cl_mem canvas_image;
} g_opencl_global;

static
int init_opencl_device(void)
{
    cl_int cl_ret;

    cl_uint platform_count;
    cl_ret = clGetPlatformIDs(0, NULL, &platform_count);
    if (cl_ret != CL_SUCCESS)
    {
        return -1;
    }
    cl_platform_id *platform_ids = (cl_platform_id*)malloc(sizeof(cl_platform_id) * platform_count);
    clGetPlatformIDs(platform_count, platform_ids, NULL);

    int device_ready = 0;
    for (cl_uint plat_idx = 0; plat_idx < platform_count; ++plat_idx)
    {
        cl_platform_id plat_id = platform_ids[plat_idx];

        cl_uint device_count;
        cl_ret = clGetDeviceIDs(plat_id, CL_DEVICE_TYPE_GPU, 0, NULL, &device_count);
        if (cl_ret != CL_SUCCESS || device_count == 0)
        {
            continue;
        }
        cl_device_id *device_ids = (cl_device_id*)malloc(sizeof(cl_device_id) * device_count);
        clGetDeviceIDs(plat_id, CL_DEVICE_TYPE_GPU, device_count, device_ids, NULL);

        for (cl_uint device_idx = 0; device_idx < device_count; ++device_idx)
        {
            cl_device_id device_id = device_ids[device_idx];

            cl_context_properties cps[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)plat_id, 0};
            cl_context device_ctx = clCreateContext(cps, 1, &device_id, NULL, NULL, &cl_ret);
            if (cl_ret != CL_SUCCESS || device_ctx == NULL)
            {
                continue;
            }
            cl_uint image_format_count = 0;
            cl_ret = clGetSupportedImageFormats(device_ctx, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, 0, NULL, &image_format_count);
            if (cl_ret != CL_SUCCESS || image_format_count == 0)
            {
                clReleaseContext(device_ctx);
                continue;
            }
            cl_image_format *image_formats = (cl_image_format*)malloc(sizeof(*image_formats) * image_format_count);
            clGetSupportedImageFormats(device_ctx, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D, image_format_count, image_formats, NULL);
            for (cl_uint i = 0; i < image_format_count; ++i)
            {
                cl_image_format *image_format = &image_formats[i];
                if (image_format->image_channel_order == CL_RGBA && 
                    image_format->image_channel_data_type == CL_UNSIGNED_INT8)
                {
                    g_opencl_global.opencl_platform = plat_id;
                    g_opencl_global.opencl_device = device_id;
                    g_opencl_global.opencl_device_context = device_ctx;
                    device_ready = 1;
                    break;
                }
            }
            free(image_formats);

            if (device_ready)
            {
                char dev_name[128];
                char dev_vendor[128];
                char dev_version[128];
                memset(dev_name, 0, sizeof(dev_name));
                memset(dev_vendor, 0, sizeof(dev_vendor));
                memset(dev_version, 0, sizeof(dev_version));
                cl_ret = clGetDeviceInfo(device_id, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
                cl_ret |= clGetDeviceInfo(device_id, CL_DEVICE_VENDOR, sizeof(dev_vendor), dev_vendor, NULL);
                cl_ret |= clGetDeviceInfo(device_id, CL_DEVICE_VERSION, sizeof(dev_version), dev_version, NULL);
                if (cl_ret == CL_SUCCESS)
                {
                    printf("init_opencl_device, seleted device, name: %s, vendor: %s, version: %s\n", dev_name, dev_vendor, dev_version);
                }

                break;
            }
            else
            {
                printf("init_opencl_device, no GPU device supporting RGBA 8bit color format\n");
                clReleaseContext(device_ctx);
            }
        }
        free(device_ids);
        
        if (device_ready)
        {
            break;
        }
    }
    free(platform_ids);

    if (device_ready)
    {
        cl_command_queue command_queue = clCreateCommandQueue(
            g_opencl_global.opencl_device_context, 
            g_opencl_global.opencl_device, 
            0, &cl_ret);
        if (cl_ret == CL_SUCCESS)
        {
            g_opencl_global.command_queue = command_queue;
            return 0;
        }
    }

    clReleaseContext(g_opencl_global.opencl_device_context);
    g_opencl_global.opencl_device_context = NULL;

    return -1;
}

static
int init_opencl_image(int w, int h)
{
    cl_int cl_ret;
    cl_image_format image_format = {CL_RGBA, CL_UNSIGNED_INT8};
    cl_mem image = clCreateImage2D(g_opencl_global.opencl_device_context, CL_MEM_WRITE_ONLY, &image_format, 
        w, h, 0, NULL, &cl_ret);
    if (cl_ret != CL_SUCCESS || image == NULL)
    {
        printf("init_opencl_image: clCreateImage2D() failed, ret: %d\n", cl_ret);
        return -1;
    }

    g_opencl_global.canvas_image = image;

    return 0;
}

static
int load_opencl_program(const char *ocl_source_file)
{
    char *ocl_source;
    size_t ocl_source_len;

    FILE *fp = fopen(ocl_source_file, "rb");
    fseek(fp, 0, SEEK_END);
    ocl_source_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    ocl_source = (char*)malloc(ocl_source_len+1);
    fread(ocl_source, 1, ocl_source_len, fp);
    ocl_source[ocl_source_len] = '0'; 
    fclose(fp);

    cl_int cl_ret;
    cl_program program = clCreateProgramWithSource(g_opencl_global.opencl_device_context, 1, &ocl_source, &ocl_source_len, &cl_ret);
    if (cl_ret != CL_SUCCESS || program == NULL)
    {
        printf("load_opencl_program, clCreateProgramWithSource() failed, ret: %d\n", cl_ret);
        free(ocl_source);
        return -1;
    }
    free(ocl_source);

    cl_ret = clBuildProgram(program, 1, &g_opencl_global.opencl_device, NULL, NULL, NULL);
    if (cl_ret != CL_SUCCESS)
    {
        char build_log[4096];
        memset(build_log, 0, sizeof(build_log));
        size_t log_len;
        clGetProgramBuildInfo(program, g_opencl_global.opencl_device, CL_PROGRAM_BUILD_LOG, sizeof(build_log), build_log, &log_len);
        printf("load_opencl_program, clBuildProgram() failed, build log:\n%s\n", build_log);

        clReleaseProgram(program);
        return -1;
    }
    cl_kernel kernel = clCreateKernel(program, "render_gradient", &cl_ret);
    if (cl_ret == CL_SUCCESS)
    {
        g_opencl_global.render_gradient_kernel = kernel;
    }
    else
    {
        printf("load_opencl_program, no render_gradient kernel was found\n");
    }
    kernel = clCreateKernel(program, "render_project_depth", &cl_ret);
    if (cl_ret == CL_SUCCESS)
    {
        g_opencl_global.render_project_depth_kernel = kernel;
    }
    else
    {
        printf("load_opencl_program, no render_project_depth kernel was found\n");
    }
    g_opencl_global.program = program;

    return 0; 
}

void uninit_cl_render(void);

int init_cl_rendler(const char *ocl_source_file, int w, int h)
{
    memset(&g_opencl_global, 0, sizeof(g_opencl_global));

    do
    {
        if (init_opencl_device() != 0)
        {
            printf("init_opencl_device() failed\n");
            break;
        }

        if (load_opencl_program(ocl_source_file) != 0)
        {
            printf("load_opencl_program() failed\n");
            break;
        }

        if (init_opencl_image(w, h))
        {
            printf("init_opencl_image() failed\n");
            break;
        }

        return 0;
    } while(0);

    uninit_cl_render();

    return -1;
}

void uninit_cl_render(void)
{
    if (g_opencl_global.canvas_image != NULL)
    {
        clReleaseMemObject(g_opencl_global.canvas_image);
        g_opencl_global.canvas_image = NULL;
    }
    if (g_opencl_global.render_gradient_kernel != NULL)
    {
        clReleaseKernel(g_opencl_global.render_gradient_kernel);
        g_opencl_global.render_gradient_kernel = NULL;
    }
    if (g_opencl_global.render_project_depth_kernel != NULL)
    {
        clReleaseKernel(g_opencl_global.render_project_depth_kernel);
        g_opencl_global.render_project_depth_kernel = NULL;
    }
    if (g_opencl_global.program != NULL)
    {
        clReleaseProgram(g_opencl_global.program);
        g_opencl_global.program = NULL;
    }
    if (g_opencl_global.command_queue != NULL)
    {
        clReleaseCommandQueue(g_opencl_global.command_queue);
        g_opencl_global.command_queue = NULL;
    }
    if (g_opencl_global.opencl_device_context != NULL)
    {
        clReleaseContext(g_opencl_global.opencl_device_context);
        g_opencl_global.opencl_device_context = NULL;
    }

    return;
}

int render_gradient_opencl(uint8_t* pixel, int w, int h, int pitch)
{
    cl_int cl_ret;
    cl_context device_context = g_opencl_global.opencl_device_context;
    cl_command_queue command_queue = g_opencl_global.command_queue;
    cl_kernel render_gradient_kernel = g_opencl_global.render_gradient_kernel;

    /* 前面的工作可事先进行，不算在render过程 */
    uint64_t ts1 = now_ms();
    cl_ret = clSetKernelArg(render_gradient_kernel, 0, sizeof(g_opencl_global.canvas_image), &g_opencl_global.canvas_image);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_gradient_opencl: clSetKernelArg() failed, ret: %d\n", cl_ret);
        return -1;
    }

    size_t global_work_size[2] = {w, h};
    size_t local_work_size[2] = {16, 16};
    cl_event result_event = NULL;
    cl_ret = clEnqueueNDRangeKernel(command_queue, render_gradient_kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, &result_event);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_gradient_opencl: clEnqueueNDRangeKernel() failed, ret: %d\n", cl_ret);
        return -1;
    }

    size_t origin[3] = {0, 0, 0};
    size_t region[3] = {w, h, 1};
    cl_ret = clEnqueueReadImage(command_queue, g_opencl_global.canvas_image, CL_TRUE, origin, region, pitch, 0, pixel, 1, &result_event, NULL);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_gradient_opencl: clEnqueueReadImage() failed\n");
        clReleaseEvent(result_event);
        return -1;
    }
    uint64_t ts2 = now_ms();
    printf("render_gradient_opencl, width: %d, height: %d, time elapsed: %" PRIu64 "ms\n", w, h, (ts2-ts1));

    clReleaseEvent(result_event);

    return 0;
}

int render_project_depth_opencl(uint8_t* pixel, int w, int h, int pitch)
{
    cl_int cl_ret;
    cl_context device_context = g_opencl_global.opencl_device_context;
    cl_command_queue command_queue = g_opencl_global.command_queue;
    cl_kernel render_project_depth_kernel = g_opencl_global.render_project_depth_kernel;

    project_camera_t camera;
    setup_project_camera(&camera);
    cl_mem cl_project_camera = clCreateBuffer(device_context, CL_MEM_READ_ONLY, sizeof(camera), NULL, &cl_ret);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_project_depth_opencl, clCreateBuffer() for project_camera failed, ret: %d\n", cl_ret);
        return -1;
    }
    cl_ret = clEnqueueWriteBuffer(command_queue, cl_project_camera, CL_TRUE, 0, sizeof(camera), &camera, 0, NULL, NULL);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_project_depth_opencl, clEnqueueWriteBuffer() for project_camera failed, ret: %d\n", cl_ret);
        clReleaseMemObject(cl_project_camera);
        return -1;
    }

    sphere_t sphere;
    setup_sphere(&sphere);
    cl_mem cl_sphere = clCreateBuffer(device_context, CL_MEM_READ_ONLY, sizeof(sphere), NULL, &cl_ret);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_project_depth_opencl, clCreateBuffer() for sphere failed, ret: %d\n", cl_ret);
        clReleaseMemObject(cl_project_camera);
        return -1;
    }
    cl_ret = clEnqueueWriteBuffer(command_queue, cl_sphere, CL_TRUE, 0, sizeof(sphere), &sphere, 0, NULL, NULL);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_project_depth_opencl, clEnqueueWriteBuffer() for sphere failed, ret: %d\n", cl_ret);
        clReleaseMemObject(cl_sphere);
        clReleaseMemObject(cl_project_camera);
        return -1;
    }

    uint64_t ts1 = now_ms();
    do
    {
        cl_ret = clSetKernelArg(render_project_depth_kernel, 0, sizeof(cl_project_camera), &cl_project_camera);
        if (cl_ret != CL_SUCCESS)
        {
            printf("render_project_depth_opencl: clSetKernelArg(cl_project_camera) failed, ret: %d\n", cl_ret);
            break;
        }
        cl_ret = clSetKernelArg(render_project_depth_kernel, 1, sizeof(cl_sphere), &cl_sphere);
        if (cl_ret != CL_SUCCESS)
        {
            printf("render_project_depth_opencl: clSetKernelArg(cl_sphere) failed, ret: %d\n", cl_ret);
            break;
        }
        cl_ret = clSetKernelArg(render_project_depth_kernel, 2, sizeof(g_opencl_global.canvas_image), &g_opencl_global.canvas_image);
        if (cl_ret != CL_SUCCESS)
        {
            printf("render_project_depth_opencl: clSetKernelArg(image) failed, ret: %d\n", cl_ret);
            break;
        }
    }while(0);
    if (cl_ret != CL_SUCCESS)
    {
        clReleaseMemObject(cl_sphere);
        clReleaseMemObject(cl_project_camera);
        return -1;
    }

    cl_uint work_dim = 2;
    size_t global_work_size[2] = {w, h};
    size_t local_work_size[2] = {16, 16};
    cl_event result_event = NULL;
    cl_ret = clEnqueueNDRangeKernel(command_queue, render_project_depth_kernel, work_dim, NULL, global_work_size, local_work_size, 0, NULL, &result_event);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_project_depth_opencl: clEnqueueNDRangeKernel() failed, ret: %d\n", cl_ret);
        clReleaseMemObject(cl_sphere);
        clReleaseMemObject(cl_project_camera);
        return -1;
    }

    size_t origin[3] = {0, 0, 0};
    size_t region[3] = {w, h, 1};
    cl_ret = clEnqueueReadImage(command_queue, g_opencl_global.canvas_image, CL_TRUE, origin, region, pitch, 0, pixel, 1, &result_event, NULL);
    if (cl_ret != CL_SUCCESS)
    {
        printf("render_project_depth_opencl: clEnqueueReadImage() failed, ret: %d\n", cl_ret);
        clReleaseEvent(result_event);
        clReleaseMemObject(cl_sphere);
        clReleaseMemObject(cl_project_camera);
        return -1;
    }
    uint64_t ts2 = now_ms();
    printf("render_project_depth_opencl, width: %d, height: %d, time elapsed: %" PRIu64 "ms\n", w, h, (ts2-ts1));

    clReleaseEvent(result_event);
    clReleaseMemObject(cl_sphere);
    clReleaseMemObject(cl_project_camera);

    return 0;
}
