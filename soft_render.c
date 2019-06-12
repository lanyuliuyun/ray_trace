
#include "common.h"

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#define _USE_MATH_DEFINES
#include <math.h>

/* 基础的几何向量运算 */

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
    float f = sqrtf((v->x * v->x) + v->y * v->y + (v->z * v->z));
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

static inline
int same_direction(const direction_t* d1, const direction_t* d2)
{
    if (d1->x != d2->x || d1->y != d2->y || d1->z != d2->z)
    {
        return 0;
    }

    return 1;
}

static
void ray_getpoint(point_t* point, const ray_t *ray, double t)
{
    point_t delta = ray->direction;
    float3_multiply((float3_t*)&delta, t);
    *point = ray->origin;
    float3_add((float3_t*)point, (float3_t*)&delta);
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
            result->distance = -DdotV - sqrtf(discr);
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

typedef struct pixel_color {uint8_t b; uint8_t g; uint8_t r; uint8_t a;} pixel_color_t;
pixel_color_t color_black = {0, 0, 0, 255};
pixel_color_t color_white = {255, 255, 255, 255};

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
                     * sqrtf(300^2 - 210^2) = 214.24
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
