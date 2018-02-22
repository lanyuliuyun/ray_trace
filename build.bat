
@echo off

SET SDL_ROOT=D:\SDL2-2.0.5
SET OPENCL_ROOT=C:\CUDAv8.0_SDK

cl /nologo /utf-8 /Zi ^
    /I%SDL_ROOT%\include /DSDL_MAIN_HANDLED ^
    /I%OPENCL_ROOT%\include ^
    .\ray_trace.c ^
    /link ^
    /LIBPATH:%SDL_ROOT%\lib\x64 SDL2.lib ^
    /LIBPATH:%OPENCL_ROOT%\lib\x64 OpenCL.lib ^
    /OUT:ray_trace.exe
