
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

typedef struct float3 { float x; float y; float z;} float3_t;

typedef float3_t point_t;

/* direction 是单位向量 */
typedef float3_t direction_t;

extern const float3_t float3_zero;
extern const direction_t direction_none;
extern const point_t point_zero;

/* 如果 direction 等于 direction_none , 表示该条光线为无效的光线 */
typedef struct ray {point_t origin; direction_t direction;} ray_t;

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

typedef struct sphere
{
    point_t center;
    float pad_for_center;

    float radius;
    float sqr_radius;
    float pad[2];
} sphere_t;

extern void setup_project_camera(project_camera_t *camera);

extern void setup_sphere(sphere_t *sphere);

extern uint64_t now_ms(void);

#endif
