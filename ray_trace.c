
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

extern int init_cl_rendler(const char *ocl_source_file, int w, int h);
extern void uninit_cl_render(void);
extern int render_gradient_opencl(uint8_t* pixel, int w, int h, int pitch);
extern int render_project_depth_opencl(uint8_t* pixel, int w, int h, int pitch);

extern void render_gradient_soft(uint8_t* pixel, int w, int h, int pitch);
extern void render_project_depth_soft(uint8_t* pixel, int w, int h, int pitch);

/********************************************************************************/

int main(int argc, char *argv[])
{
    int win_w = 640, win_h = 480;
    const char *cl_source_file = "render.cl";
    init_cl_rendler(cl_source_file, win_w, win_h);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("Render Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, 0);
    SDL_Surface *surface = SDL_GetWindowSurface(window);

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

    uninit_cl_render();

    return 0;
}
