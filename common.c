
#include "common.h"

#define _USE_MATH_DEFINES
#include <math.h>

const float3_t float3_zero = {0.0, 0.0, 0.0};
const point_t point_zero = {0.0, 0.0, 0.0};
const direction_t direction_none = {0.0, 0.0, 0.0};

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

    camera->left_angle_tan = -tanf((left_fov / 180) * M_PI);
    camera->right_angle_tan = tanf((right_fov / 180) * M_PI);
    camera->top_angle_tan = tanf((top_fov / 180) * M_PI);
    camera->bottom_angle_tan = -tanf((bottom_fov / 180) * M_PI);

    return;
}

void setup_project_camera(project_camera_t *camera)
{
    point_t eye = {320.0, 240.0, 180.0};
    direction_t front = {0.0, 0.0, -1.0};
    /* 为避免不能完全看到目标场景全貌，视角范围可以尽量大一些 */
    project_camera_init(camera, &eye, &front, 80, 80, 80, 80);

    return;
}

static
void sphere_init(sphere_t* sphere, const point_t* center, float radius)
{
    sphere->center = *center;
    sphere->radius = radius;
    sphere->sqr_radius = radius * radius;

    return;
}

void setup_sphere(sphere_t *sphere)
{
    point_t center = {320.0, 240.0, -120.0};
    sphere_init(sphere, &center, 210);
    return;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
uint64_t now_ms(void)
{
    return GetTickCount();
}
