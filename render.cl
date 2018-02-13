
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel
void render_gradient(__write_only image2d_t out_image)
{
    size_t width = get_global_size(0);
    size_t height = get_global_size(1);
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);

    int2 cord = (int2)(x, y);
    /* RGBA */
    uint4 pixel;
    pixel.x = 0;
    pixel.y = (uchar)(((float)y / height) * 255);
    pixel.z = (uchar)(((float)x / width) * 255);
    pixel.w = 255;
    write_imageui(out_image, cord, pixel);

    return;
}

/********************************************************************************/

typedef struct ray
{
    float3 origin; 
    float3 direction;
} ray_t;

typedef struct project_camera
{
    /* 基础字面属性 */
    float3 eye;
    float3 front;
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
void project_camera_generateRay
(
    ray_t *ray, 
    __global const project_camera_t* camera, 
    float3 dest_point
)
{
    float3 delta = dest_point - camera->eye;
    ray->direction = (float3)(0.0, 0.0, 0.0);

    /* 为简化计算，下面的计算有一个假设： 摄像机的观察方向与z轴平行
     * 严格的讲，需要根据观察点的位置和方向来具体计算
     */

    /* 要求目标点必须在 camera 前方, 即z轴坐标小于 eye.z */
    if (delta.z >= 0)
    {
        return;
    }

    /* 根据角度判断是否在视野范围内 */
    /* BUG: 此处和预期不同，会全部认为不在视野范围内，和soft render版本不同！ */
  #if 1
    float h_tan = delta.x / (-delta.z);
    float v_tan = delta.y / (-delta.z);
    if (h_tan < camera->left_angle_tan || h_tan > camera->right_angle_tan || 
        v_tan < camera->bottom_angle_tan || v_tan > camera->top_angle_tan)
    {
        return;
    }
  #endif

    ray->origin = camera->eye;
    ray->direction = normalize(delta);

    return;
}

static
float3 ray_getpoint(const ray_t *ray, float t)
{
    float3 delta = ray->direction * t;
    return (ray->origin + delta);
}

/****************************************************************************************************/

typedef struct sphere
{
    float3 center;
    float radius;
    float sqr_radius;
} sphere_t;

typedef struct intersect_result
{
    bool hit;
    float distance;
    float3 position;
    float3 normal;
} intersect_result_t;

/* 光线与球面的相交计算，得出离光线原点最近的交点坐标 */
static
void sphere_intersect
(
    intersect_result_t* intersect_result, 
    __global sphere_t *sphere,
    const ray_t* ray
)
{
    float3 delta = ray->origin - sphere->center;

    float DdotV = dot(ray->direction, delta);
    if (DdotV > 0)
    {
        intersect_result->hit = false;
    }
    else
    {
        float delta_length = length(delta);
        float a0 = delta_length * delta_length - sphere->sqr_radius;
        float discr = DdotV * DdotV - a0;
        if (discr >= 0)
        {
            intersect_result->hit = true;
            intersect_result->distance = -DdotV - sqrt(discr);
            intersect_result->position = ray_getpoint(ray, intersect_result->distance);
            delta = intersect_result->position - sphere->center;
            intersect_result->normal = normalize(delta);
        }
        else
        {
            intersect_result->hit = false;
        }
    }

    return;
}

/****************************************************************************************************/

__kernel
void render_project_depth
(
    __global project_camera_t *project_camera,
    __global sphere_t *sphere,
    __write_only image2d_t out_image
)
{
    size_t width = get_global_size(0);
    size_t height = get_global_size(1);
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);

    int2 cord = (int2)(x, y);
    uint4 pixel;

    /* 国际象棋棋盘背景色 */
    int x_block_idx = x / 40;
    int y_block_idx = y / 40;
    int result = (x_block_idx - y_block_idx) & 0x01;
    if (result)
    {
        pixel.x = 255;
        pixel.y = 255;
        pixel.z = 255;
        pixel.w = 255;
    }
    else
    {
        pixel.x = 0;
        pixel.y = 0;
        pixel.z = 0;
        pixel.w = 255;
    }

    /* 将窗口坐标转换到影像平面坐标 */
    float3 point = (float3)(x, (height - y), 0.0);
    ray_t ray;
    intersect_result_t intersect_result;
    project_camera_generateRay(&ray, project_camera, point);
    if (ray.direction.x != 0 || ray.direction.y != 0 || ray.direction.z != 0)
    {
        sphere_intersect(&intersect_result, sphere, &ray);
        if (intersect_result.hit)
        {
          #if 1
            float value = (intersect_result.distance / 200) * 255;
            if (value > 255)
            {
                value = 255;
            }
            value = 255 - value;
            pixel.x = value;
            pixel.y = value;
            pixel.z = value;
          #else
            pixel.x = (result.normal.x + 1) * 128;
            pixel.y = (result.normal.y + 1) * 128;
            pixel.z = (result.normal.z + 1) * 128;
          #endif
        }
        else
        {
            /* 未相交时，保持原来的背景色 */

            /* 调试，写入红色 */
            pixel.x = 0;
            pixel.y = 0;
            pixel.z = 255;
        }
    }
    else
    {
        /* 影像平面上的点，不在摄像机视角范围内, 保持原来的背景色 */

        /* 调试，写入蓝色 */
        pixel.x = 255;
        pixel.y = 0;
        pixel.z = 0;
    }
    
    write_imageui(out_image, cord, pixel);

    return;
}
