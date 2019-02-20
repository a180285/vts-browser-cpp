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

#ifndef RENDERER_HPP_deh4f6d4hj
#define RENDERER_HPP_deh4f6d4hj

#include <assert.h>
#include <sstream>
#include <iomanip>

#include <vts-browser/log.hpp>
#include <vts-browser/math.hpp>
#include <vts-browser/buffer.hpp>

#include "include/vts-renderer/classes.hpp"
#include "include/vts-renderer/renderer.hpp"

namespace vts
{

class CameraDraws;
class DrawSurfaceTask;
class DrawSimpleTask;

namespace renderer
{

namespace priv
{

extern uint32 maxAntialiasingSamples;
extern float maxAnisotropySamples;

class RendererImpl
{
public:
    Renderer *const rendererApi;

    RenderVariables vars;
    RenderOptions options;

    std::shared_ptr<Texture> texCompas;
    std::shared_ptr<Shader> shaderTexture;
    std::shared_ptr<class ShaderAtm> shaderSurface;
    std::shared_ptr<class ShaderAtm> shaderBackground;
    std::shared_ptr<Shader> shaderInfographic;
    std::shared_ptr<Shader> shaderCopyDepth;
    std::shared_ptr<Shader> shaderGeodataLine;
    std::shared_ptr<Shader> shaderGeodataPoint;
    std::shared_ptr<Shader> shaderGeodataPointLabel;
    std::shared_ptr<Mesh> meshQuad; // positions: -1 .. 1
    std::shared_ptr<Mesh> meshRect; // positions: 0 .. 1
    std::shared_ptr<UniformBuffer> uboGeodataCamera;
    std::shared_ptr<UniformBuffer> uboAtm;

    CameraDraws *draws;
    const MapCelestialBody *body;
    Texture *atmosphereDensityTexture;

    mat4 view;
    mat4 viewInv;
    mat4 proj;
    mat4 viewProj;

    uint32 widthPrev;
    uint32 heightPrev;
    uint32 antialiasingPrev;

    bool projected;

    static void clearGlState();

    RendererImpl(Renderer *rendererApi);
    ~RendererImpl();
    void drawSurface(const DrawSurfaceTask &t);
    void drawInfographic(const DrawSimpleTask &t);
    void enableClipDistance(bool enable);
    void updateFramebuffers();
    void renderGeodata();
    void render();
    void initializeGeodata();
    void initialize();
    void finalize();
    void updateAtmosphereBuffer();
    void renderCompass(const double screenPosSize[3],
        const double mapRotation[3]);
    void getWorldPosition(const double screenPos[2], double worldPos[3]);
};

} // namespace priv

using namespace priv;

} // namespace renderer

} // namespace vts

#endif
