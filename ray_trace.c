
#include <SDL2/SDL.h>
#include <CL/cl.h>

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static inline
uint64_t now_ms(void)
{
    return GetTickCount();
}

/********************************************************************************/

/* 基础的几何向量运算 */
typedef struct float3 { float x; float y; float z;} float3_t;
const float3_t float3_zero = {0.0, 0.0, 0.0};

static inline
void float3_add(float3_t *v, const float3_t* delta)
{
  v->x += delta->x;
  v->y += delta->y;
  v->z += delta->z;
}

static inline
void float3_subtract(float3_t *v, const float3_t* delta)
{
  v->x -= delta->x;
  v->y -= delta->y;
  v->z -= delta->z;
}

static inline
void float3_multiply(float3_t* v, float f)
{
    v->x *= f;
    v->y *= f;
    v->z *= f;
}

static inline
void float3_div(float3_t* v, float f)
{
    v->x /= f;
    v->y /= f;
    v->z /= f;
}

static inline
float float3_length(const float3_t *v)
{
    float f = sqrt((v->x * v->x) + v->y * v->y + (v->z * v->z));
    return f;
}

static inline
float float3_sqrlength(const float3_t *v)
{
    float f = (v->x * v->x) + (v->y * v->y) + (v->z * v->z);
    return f;
}

static inline
void float3_normalize(float3_t *n, const float3_t *v)
{
    *n = *v;
    float length = float3_length(v);
    float3_div(n, length);
}

static inline
float float3_dot(const float3_t* v1, const float3_t* v2)
{
    return (v1->x * v2->x + v1->y * v2->y + v1->z * v2->z);
}

static inline
void float3_cross(float3_t *vo, const float3_t* v1, const float3_t* v2)
{
    float x = v1->y * v2->z - v1->z * v2->y;
    float y = v1->z * v2->x - v1->x * v2->z;
    float z = v1->x * v2->y - v1->y * v2->x;
    vo->x = x;
    vo->y = y;
    vo->z = z;
}

/********************************************************************************/

typedef float3_t point_t;
const point_t point_zero = {0.0, 0.0, 0.0};

/* direction 是单位向量 */
typedef float3_t direction_t;
const direction_t direction_none = {0.0, 0.0, 0.0};
int same_direction(const direction_t* d1, const direction_t* d2)
{
    if (d1->x != d2->x || d1->y != d2->y || d1->z != d2->z)
    {
        return 0;
    }

    return 1;
}

/* 如果 direction 等于 direction_none , 表示该条光线为无效的光线 */
typedef struct ray {point_t origin; direction_t direction;} ray_t;

static
void ray_getpoint(point_t* point, const ray_t *ray, double t)
{
    point_t delta = ray->direction;
    float3_multiply((float3_t*)&delta, t);
    *point = ray->origin;
    float3_add((float3_t*)point, (float3_t*)&delta);
}

/* 透视摄像机 */

typedef struct project_camera
{
    /* 基础字面属性 */
    point_t eye;
    float pad_for_eye;
    
    direction_t front;
    float pad_for_front;
    
    /* 视角单位为度 */
    float left_fov;
    float right_fov;
    float top_fov;
    float bottom_fov;

    /* 计算属性 */

    /* 视角方向边界, 水平和垂直两面个面的角度边界 */
    float left_angle_tan;
    float right_angle_tan;
    float top_angle_tan;
    float bottom_angle_tan;
} project_camera_t;

static
void project_camera_init
(
    project_camera_t* camera, const point_t* eye, const direction_t* front, 
    float left_fov, float right_fov, float top_fov, float bottom_fov
)
{
    camera->eye = *eye;
    camera->front = *front;
    camera->left_fov = left_fov;
    camera->right_fov = right_fov;
    camera->top_fov = top_fov;
    camera->bottom_fov = bottom_fov;

    camera->left_angle_tan = -tan((left_fov / 180) * M_PI);
    camera->right_angle_tan = tan((right_fov / 180) * M_PI);
    camera->top_angle_tan = tan((top_fov / 180) * M_PI);
    camera->bottom_angle_tan = -tan((bottom_fov / 180) * M_PI);

    return;
}

/* 获取一条从摄像机到目标点的光线，如果结果为无效光线，说明目标点，不在摄像机的视野范围内 */
static
void project_camera_generateRay
(
    ray_t *ray, 
    const project_camera_t* camera, 
    const point_t* dest
)
{
    point_t delta = *dest;
    float3_subtract((float3_t*)&delta, (float3_t*)&camera->eye);

    ray->direction = direction_none;

    /* 为简化计算，下面的计算有一个假设： 摄像机的观察方向与z轴平行
    * 严格的讲，需要根据观察点的位置和方向来具体计算
    */

    /* 要求目标点必须在 camera 前方, 即z轴坐标小于 eye.z */
    if (delta.z >= 0)
    {
        return;
    }

    /* 根据角度判断是否在视野范围内 */
    float h_tan = delta.x / (-delta.z);
    float v_tan = delta.y / (-delta.z);
    if (h_tan < camera->left_angle_tan || h_tan > camera->right_angle_tan || 
        v_tan < camera->bottom_angle_tan || v_tan > camera->top_angle_tan)
    {
        return;
    }

    ray->origin = camera->eye;
    float3_normalize(&ray->direction, &delta);

    return;
}

/********************************************************************************/

typedef struct intersect_result
{
    const void* geometry;
    float distance;
    point_t position;
    float3_t normal;
} intersect_result_t;
const intersect_result_t intersect_nohit = {NULL, 0, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};

/********************************************************************************/

typedef struct sphere
{
    point_t center;
    float pad_for_center;

    float radius;
    float sqr_radius;
    float pad[2];
}sphere_t;

static
void sphere_init(sphere_t* sphere, const point_t* center, float radius)
{
    sphere->center = *center;
    sphere->radius = radius;
    sphere->sqr_radius = radius * radius;

  return;
}

/* 光线与球面的相交计算，得出离光线原点最近的交点坐标 */
static
void sphere_intersect(intersect_result_t* result, const sphere_t* sphere, const ray_t* ray)
{
    float3_t delta = *(const float3_t*)&ray->origin;
    float3_subtract((float3_t*)&delta, (const float3_t*)&sphere->center);

    float DdotV = float3_dot((const float3_t*)&ray->direction, &delta);
    if (DdotV > 0)
    {
        *result = intersect_nohit;
    }
    else
    {
        float a0 = float3_sqrlength(&delta) - sphere->sqr_radius;
        float discr = DdotV * DdotV - a0;
        if (discr >= 0)
        {
            result->geometry = sphere;
            result->distance = -DdotV - sqrt(discr);
            ray_getpoint(&result->position, ray, result->distance);
            delta = result->position;
            float3_subtract(&delta, (const float3_t*)&sphere->center);
            float3_normalize(&result->normal, &delta);
        }
        else
        {
            *result = intersect_nohit;
        }
    }

    return;
}

/********************************************************************************/

static
void setup_project_camera(project_camera_t *camera)
{
    point_t eye = {320.0, 240.0, 180.0};
    direction_t front = {0.0, 0.0, -1.0};
    /* 为避免不能完全看到目标场景全貌，视角范围可以尽量大一些 */
    project_camera_init(camera, &eye, &front, 80, 80, 80, 80);

    return;
}

static
void setup_sphere(sphere_t *sphere)
{
    point_t center = {320.0, 240.0, -120.0};
    sphere_init(sphere, &center, 210);
    return;
}

/********************************************************************************/

typedef struct pixel_color {uint8_t b; uint8_t g; uint8_t r; uint8_t a;} pixel_color_t;
pixel_color_t color_black = {0, 0, 0, 255};
pixel_color_t color_white = {255, 255, 255, 255};

static
void render_gradient_soft(uint8_t* pixel, int w, int h, int pitch)
{
    int i, j;
    uint8_t *line;

    uint64_t ts1 = now_ms();
    line = pixel;
    for (j = 0; j < h; ++j)
    {
        pixel_color_t *pixel_color = (pixel_color_t*)line;
        for (i = 0; i < w; ++i)
        {
            pixel_color->b = 0;
            pixel_color->g = (uint8_t)(((float)j / h) * 255);
            pixel_color->r = (uint8_t)(((float)i / w) * 255);
            pixel_color->a = 1;

            pixel_color++;
        }
        line += pitch;
    }
    uint64_t ts2 = now_ms();

    printf("render_gradient_soft, width: %d, height: %d, time elapsed: %" PRIu64 "ms\n", w, h, (ts2-ts1));

    return;
}

static
void render_project_depth_soft(uint8_t* pixel, int w, int h, int pitch)
{
    project_camera_t camera;
    setup_project_camera(&camera);

    sphere_t sphere;
    setup_sphere(&sphere);

    int i, j;
    uint8_t *line;

    point_t point;
    ray_t ray;
    intersect_result_t intersect_result;

    /* 影像平面为 {0 <= x < 640, 0 <= y < 480, z = 0} */

    uint64_t ts1 = now_ms();
    line = pixel;
    for (j = 0; j < h; ++j)
    {
        pixel_color_t *pixel_color = (pixel_color_t*)line;
        for (i = 0; i < w; ++i)
        {
            int x_block_count = i / 40;
            int y_block_count = j / 40;
            if ((x_block_count - y_block_count) & 0x01)
            {
                *pixel_color = color_white;
            }
            else
            {
                *pixel_color = color_black;
            }

            /* 此处将窗口平面的坐标映射到影像平面 */
            point.x = i;
            point.y = h - j;
            point.z = 0.0;

            project_camera_generateRay(&ray, &camera, &point);
            if (!same_direction(&ray.direction, &direction_none))
            {
                sphere_intersect(&intersect_result, &sphere, &ray);
                if (intersect_result.geometry)
                {
                 #if 1
                    /* 相切点的距离最大，当前坐标和尺寸时，最大距离约为
                     * sqrt(300^2 - 210^2) = 214.24
                     */
                    float value = (intersect_result.distance / 200) * 255;
                    if (value > 255)
                    {
                        value = 255;
                    }
                    value = 255 - value;
                    pixel_color->r = value;
                    pixel_color->g = value;
                    pixel_color->b = value;
                 #else
                    pixel_color->r = (intersect_result.normal.x + 1) * 128;
                    pixel_color->g = (intersect_result.normal.y + 1) * 128;
                    pixel_color->b = (intersect_result.normal.z + 1) * 128;
                 #endif
                }
                else
                {
                    /* 未相交时，保持原来的背景色 */
                }
            }
            else
            {
                // 影像平面上的点，不在摄像机视角范围内
            }
            pixel_color++;
        }
        line += pitch;
    }
    uint64_t ts2 = now_ms();

    printf("render_project_depth_soft, width: %d, height: %d, time elapsed: %" PRIu64 "ms\n", w, h, (ts2-ts1));

    return;
}

/********************************************************************************/

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
void uninit_opencl_device(void)
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

static
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

static
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

/********************************************************************************/

int main(int argc, char *argv[])
{
    const char *cl_source_file = "render.cl";

    memset(&g_opencl_global, 0, sizeof(g_opencl_global));
    if (init_opencl_device() != 0)
    {
        printf("init_opencl_device() failed\n");
        return 0;
    }
    if (load_opencl_program(cl_source_file) != 0)
    {
        printf("load_opencl_program() failed\n");
        uninit_opencl_device();
        return 0;
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("Render Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, 0);
    SDL_Surface *surface = SDL_GetWindowSurface(window);

    init_opencl_image(surface->w, surface->h);

    int render_ok = 0;
    SDL_LockSurface(surface);
    render_ok = render_project_depth_opencl((uint8_t*)surface->pixels, surface->w, surface->h, surface->pitch) == 0;
    SDL_UnlockSurface(surface);
    if (render_ok)
    {
        SDL_UpdateWindowSurface(window);
    }

    while (1)
    {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 500))
        {
            if (event.type == SDL_KEYUP)
            {
                SDL_Scancode key_scancode = event.key.keysym.scancode;
                if (key_scancode == SDL_SCANCODE_1)
                {
                    SDL_LockSurface(surface);
                    render_gradient_soft((uint8_t*)surface->pixels, surface->w, surface->h, surface->pitch);
                    SDL_UpdateWindowSurface(window);
                    SDL_UnlockSurface(surface);
                }
                else if (key_scancode == SDL_SCANCODE_2)
                {
                    SDL_LockSurface(surface);
                    render_ok = render_gradient_opencl((uint8_t*)surface->pixels, surface->w, surface->h, surface->pitch) == 0;
                    SDL_UnlockSurface(surface);
                    if (render_ok)
                    {
                        SDL_UpdateWindowSurface(window);
                    }
                }
                else if (key_scancode == SDL_SCANCODE_3)
                {
                    SDL_LockSurface(surface);
                    render_project_depth_soft((uint8_t*)surface->pixels, surface->w, surface->h, surface->pitch);
                    SDL_UnlockSurface(surface);
                    SDL_UpdateWindowSurface(window);
                }
                else if (key_scancode == SDL_SCANCODE_4)
                {
                    SDL_LockSurface(surface);
                    render_ok = render_project_depth_opencl((uint8_t*)surface->pixels, surface->w, surface->h, surface->pitch) == 0;
                    SDL_UnlockSurface(surface);
                    if (render_ok)
                    {
                        SDL_UpdateWindowSurface(window);
                    }
                }
            }
            else if (event.type == SDL_QUIT)
            {
                break;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    uninit_opencl_device();

    return 0;
}
