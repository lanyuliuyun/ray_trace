
@echo off

SET SDL_ROOT=E:\opensource\SDL2-2.0.8
SET OPENCL_ROOT=C:\CUDA_v10.0

cl /nologo /utf-8 /Zi ^
    /I%SDL_ROOT%\include /DSDL_MAIN_HANDLED ^
    /I%OPENCL_ROOT%\include ^
    .\ray_trace.c .\soft_render.c .\cl_render.c .\common.c ^
    /link ^
    /LIBPATH:%SDL_ROOT%\lib\x64 SDL2.lib ^
    /LIBPATH:%OPENCL_ROOT%\lib\x64 OpenCL.lib ^
    /OUT:ray_trace.exe
