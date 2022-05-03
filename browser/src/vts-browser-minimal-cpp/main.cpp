/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <vts-browser/log.hpp>
#include <vts-browser/map.hpp>
#include <vts-browser/camera.hpp>
#include <vts-browser/cameraOptions.hpp>
#include <vts-browser/navigation.hpp>
#include <vts-browser/navigationOptions.hpp>
#include <vts-renderer/renderer.hpp>
#include <vts-renderer/highPerformanceGpuHint.h>

#include <thread>
#include <stdexcept>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

SDL_Window *window;
SDL_GLContext renderContext;
SDL_GLContext dataContext;
std::shared_ptr<vts::Map> map;
std::shared_ptr<vts::Camera> cam;
std::shared_ptr<vts::Navigation> nav;
std::shared_ptr<vts::renderer::RenderContext> context;
std::shared_ptr<vts::renderer::RenderView> view;
std::thread dataThread;
bool shouldClose = false;

void dataEntry()
{
    vts::setLogThreadName("data");

    // the browser uses separate thread for uploading resources to gpu memory
    //   this thread must have access to an OpenGL context
    //   and the context must be shared with the one used for rendering
    SDL_GL_MakeCurrent(window, dataContext);
    vts::renderer::installGlDebugCallback();

    // this will block until map->renderFinalize
    //   is called in the rendering thread
    map->dataAllRun();

    SDL_GL_DeleteContext(dataContext);
    dataContext = nullptr;
}

void updateResolution()
{
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    vts::renderer::RenderOptions &ro = view->options();
    ro.width = w;
    ro.height = h;
    cam->setViewportSize(ro.width, ro.height);
}

int main(int, char *[])
{
    // initialize SDL
    vts::log(vts::LogLevel::info3, "Initializing SDL library");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
    {
        vts::log(vts::LogLevel::err4, SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
    }

    // configure parameters for OpenGL context
    // we do not need default depth buffer, the rendering library uses its own
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // use OpenGL version 3.3 core profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // enable sharing resources between multiple OpenGL contexts
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    // create window
    vts::log(vts::LogLevel::info3, "Creating window");
    {
        window = SDL_CreateWindow("vts-browser-minimal-cpp",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600,
            SDL_WINDOW_MAXIMIZED | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    }
    if (!window)
    {
        vts::log(vts::LogLevel::err4, SDL_GetError());
        throw std::runtime_error("Failed to create window");
    }

    // create OpenGL contexts
    vts::log(vts::LogLevel::info3, "Creating OpenGL contexts");
    dataContext = SDL_GL_CreateContext(window);
    renderContext = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // enable v-sync

    // make vts renderer library load OpenGL function pointers
    // this calls installGlDebugCallback for the current context too
    vts::renderer::loadGlFunctions(&SDL_GL_GetProcAddress);

    // create the renderer library context
    context = std::make_shared<vts::renderer::RenderContext>();

    // create instance of the vts::Map class
    map = std::make_shared<vts::Map>();

    // set required callbacks for creating mesh and texture resources
    context->bindLoadFunctions(map.get());

    // launch the data thread
    dataThread = std::thread(&dataEntry);

    // create a camera and acquire its navigation handle
    cam = map->createCamera();
    auto& co = cam->options();
    co.fixedTraversalDistance = 200;
    co.fixedTraversalLod = 19;
    co.traverseModeSurfaces = vts::TraverseMode::DistanceBaseFixed;
    std::string url =
            // with_ normal
//            "http://cloud-vts.huangwei.icu:8070/store/datasets/hdrp_vef_07.1650538913/mapConfig.json"
            // float uv
//            "http://cloud-vts.huangwei.icu:8070/store/datasets/hdrp_vef_07.1650537048/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/float-uv.1650537048.json/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/hdrp_vef_12.2022-04-24_13-23-14.json/mapConfig.json"
            // u32 mesh
//            "http://cloud-vts.huangwei.icu:8070/store/datasets/hdrp_vef_11.2022-04-24_11-13-44/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/hdrp_vef_13.2022-04-25_14-48-22.json/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/GlobalData_Anting/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/GlobalData_Anting_v2.json/mapConfig.json"
            // all center
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/hdrp_vef_14.2022-04-25_16-57-57.json/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/datasets/upload.2022-04-25_17-27-16/mapConfig.json"
//            "http://cloud-vts.huangwei.icu:8070/store/map-config/hdrp_vef_14.2022-04-25_17-46-22.json/mapConfig.json"
            //"http://cloud-vts.huangwei.icu:8070/store/datasets/hdrp_vef_15.2022-04-26_11-17-18/mapConfig.json"
            // road with 0 height dem
               "http://cloud-vts.huangwei.icu:8070/store/map-config/hdrp_vef_18_new.2022-05-03_11-43-41.json/mapConfig.json"
;

//    builder
//    .Append("{ \"fixedTraversalDistance\":")
//    .Append(collidersDistance)
//    .Append(", \"fixedTraversalLod\":")
//    .Append(collidersLod)
//    .Append(", \"traverseModeSurfaces\":\"fixed\", \"traverseModeGeodata\":\"none\" }");

    nav = cam->createNavigation();
//    nav->options().mode = vts::NavigationMode::Free;
    // create renderer view
    view = context->createView(cam.get());
    view->options();
    updateResolution();

    // pass a mapconfig url to the map
    map->setMapconfigPath(url);

    // acquire current time (for measuring how long each frame takes)
    uint32 lastRenderTime = SDL_GetTicks();

    // main event loop
    while (!shouldClose)
    {
        // process events
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                // handle window close
                case SDL_APP_TERMINATING:
                case SDL_QUIT:
                    shouldClose = true;
                    break;
                // handle mouse events
                case SDL_MOUSEMOTION:
                {
                    // relative mouse position
                    double p[3] = { (double)event.motion.xrel, (double)event.motion.yrel, 0 };
                    if (event.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT))
                        nav->pan(p);
                    if (event.motion.state & SDL_BUTTON(SDL_BUTTON_RIGHT))
                        nav->rotate(p);
                } break;
                case SDL_MOUSEWHEEL:
                    nav->zoom(event.wheel.y);
                    break;
                }
            }
        }

        // update navigation etc.
        updateResolution();
        uint32 currentRenderTime = SDL_GetTicks();
        map->renderUpdate((currentRenderTime - lastRenderTime) * 1e-3);
        cam->renderUpdate();
        lastRenderTime = currentRenderTime;

        // actually render the map
        view->render();
        SDL_GL_SwapWindow(window);
    }

    // release all
    nav.reset();
    cam.reset();
    view.reset();
    map->renderFinalize(); // this allows the dataThread to finish
    dataThread.join();
    map.reset();
    context.reset();

    SDL_GL_DeleteContext(renderContext);
    renderContext = nullptr;
    SDL_DestroyWindow(window);
    window = nullptr;

    return 0;
}




