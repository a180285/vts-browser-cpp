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

#include "renderer.hpp"

#include <thread>

#include <optick.h>

namespace vts { namespace renderer
{

std::string Shader::preamble =
#ifdef VTSR_OPENGLES
        "#version 300 es\n"
#else
        "#version 330 core\n"
#endif

#ifdef VTSR_NO_CLIP
        "#define VTS_NO_CLIP\n"
#elif defined(VTSR_OPENGLES) && defined(__APPLE__)
        "#extension GL_APPLE_clip_distance : require\n"
#endif

        "precision highp float;\n"
        "precision highp int;\n"
    ;

namespace privat
{

#ifndef NDEBUG

uint64 currentThreadId()
{
    return std::hash<std::thread::id>()(std::this_thread::get_id());
}

ResourceBase::ResourceBase() : thrId(currentThreadId())
{}

ResourceBase::~ResourceBase()
{
    assert(thrId == currentThreadId());
}

#endif

} // namespace privat

namespace
{

void setDebugLabel(GLenum type, GLuint id, std::string name)
{
    if (!GLAD_GL_KHR_debug || name.empty() || id == 0)
        return;
    name = name.substr(0, 200);
    glObjectLabel(type, id, name.length(), name.data());
}

} // namespace

Shader::Shader()
{
    uniformLocations.reserve(20);
}

void Shader::clear()
{
    if (id)
        glDeleteProgram(id);
    id = 0;
}

Shader::~Shader()
{
    clear();
}

void Shader::setDebugId(const std::string &name)
{
    this->debugId = name;
    setDebugLabel(GL_PROGRAM, id, debugId);
}

void Shader::bind()
{
    assert(id > 0);
    glUseProgram(id);
}

int Shader::loadShader(const std::string &source, int stage) const
{
    GLuint s = glCreateShader(stage);
    try
    {
        GLchar *src = (GLchar*)source.c_str();
        GLint len = source.length();
        glShaderSource(s, 1, &src, &len);
        glCompileShader(s);

        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        if (len > 5)
        {
            Buffer buf(len + 1);
            glGetShaderInfoLog(s, len, &len, buf.data());
            vts::log(vts::LogLevel::err3,
                     std::string("shader compilation log:\n")
                     + std::string(buf.data(), len) + "\n\n");
        }

        glGetShaderiv(s, GL_COMPILE_STATUS, &len);
        if (len != GL_TRUE)
            throw std::runtime_error("failed to compile shader");

        CHECK_GL("load shader source");
    }
    catch (...)
    {
        glDeleteShader(s);
        vts::log(vts::LogLevel::err4,
            std::string("shader source: \n") + source);
        vts::log(vts::LogLevel::err4,
            std::string("shader name: <" + debugId + ">"));
        throw;
    }
    return s;
}

void Shader::load(const std::string &vertexShader,
                  const std::string &fragmentShader)
{
    clear();
    id = glCreateProgram();
    try
    {
        GLuint v = loadShader(preamble
            + "#define VTS_STAGE_VERTEX\n"
            + vertexShader,
            GL_VERTEX_SHADER);
        GLuint f = loadShader(preamble
            + "#define VTS_STAGE_FRAGMENT\n"
            + fragmentShader,
            GL_FRAGMENT_SHADER);
        glAttachShader(id, v);
        glAttachShader(id, f);
        glLinkProgram(id);
        glDeleteShader(v);
        glDeleteShader(f);

        GLint len = 0;
        glGetProgramiv(id, GL_INFO_LOG_LENGTH, &len);
        if (len > 5)
        {
            Buffer buf(len + 1);
            glGetProgramInfoLog(id, len, &len, buf.data());
            vts::log(vts::LogLevel::err3,
                     std::string("shader link log:\n")
                     + std::string(buf.data(), len) + "\n\n");
        }

        glGetProgramiv(id, GL_LINK_STATUS, &len);
        if (len != GL_TRUE)
            throw std::runtime_error("failed to link shader");
    }
    catch(...)
    {
        glDeleteProgram(id);
        id = 0;
        throw;
    }
    setDebugId(debugId);
    CHECK_GL("load shader program");
}

void Shader::loadInternal(const std::string &vertexName,
                  const std::string &fragmentName)
{
    Buffer vert = readInternalMemoryBuffer(vertexName);
    Buffer frag = readInternalMemoryBuffer(fragmentName);
    load(vert.str(), frag.str());
}

void Shader::uniformMat4(uint32 location, const float *value, uint32 count)
{
    glUniformMatrix4fv(uniformLocations[location], count, GL_FALSE, value);
}

void Shader::uniformMat3(uint32 location, const float *value, uint32 count)
{
    glUniformMatrix3fv(uniformLocations[location], count, GL_FALSE, value);
}

void Shader::uniformVec4(uint32 location, const float *value, uint32 count)
{
    static_assert(sizeof(float) == sizeof(GLfloat), "incompatible types");
    glUniform4fv(uniformLocations[location], count, value);
}

void Shader::uniformVec3(uint32 location, const float *value, uint32 count)
{
    glUniform3fv(uniformLocations[location], count, value);
}

void Shader::uniformVec2(uint32 location, const float *value, uint32 count)
{
    glUniform2fv(uniformLocations[location], count, value);
}

void Shader::uniformVec4(uint32 location, const int *value, uint32 count)
{
    static_assert(sizeof(int) == sizeof(GLint), "incompatible types");
    glUniform4iv(uniformLocations[location], count, value);
}

void Shader::uniformVec3(uint32 location, const int *value, uint32 count)
{
    glUniform3iv(uniformLocations[location], count, value);
}

void Shader::uniformVec2(uint32 location, const int *value, uint32 count)
{
    glUniform2iv(uniformLocations[location], count, value);
}

void Shader::uniform(uint32 location, float value)
{
    glUniform1f(uniformLocations[location], value);
}

void Shader::uniform(uint32 location, int value)
{
    glUniform1i(uniformLocations[location], value);
}

void Shader::uniform(uint32 location, const float *value, uint32 count)
{
    glUniform1fv(uniformLocations[location], count, value);
}

void Shader::uniform(uint32 location, const int *value, uint32 count)
{
    glUniform1iv(uniformLocations[location], count, value);
}

uint32 Shader::getId() const
{
    return id;
}

uint32 Shader::loadUniformLocations(const std::vector<const char *> &names)
{
    bind();
    uint32 res = uniformLocations.size();
    for (auto &it : names)
        uniformLocations.push_back(glGetUniformLocation(id, it));
    return res;
}

void Shader::bindTextureLocations(
    const std::vector<std::pair<const char *, uint32>> &binds)
{
    bind();
    for (auto &it : binds)
        glUniform1i(glGetUniformLocation(id, it.first), it.second);
}

void Shader::bindUniformBlockLocations(
    const std::vector<std::pair<const char *, uint32>> &binds)
{
    for (auto &it : binds)
        glUniformBlockBinding(id,
            glGetUniformBlockIndex(id, it.first), it.second);
}

Texture::Texture()
{}

void Texture::clear()
{
    if (id)
        glDeleteTextures(1, &id);
    id = 0;
}

Texture::~Texture()
{
    clear();
}

void Texture::setDebugId(const std::string &name)
{
    this->debugId = name;
    setDebugLabel(GL_TEXTURE, id, name);
}

void Texture::bind()
{
    assert(id > 0);
    glBindTexture(GL_TEXTURE_2D, id);
}

namespace
{

GLenum findInternalFormat(const GpuTextureSpec &spec)
{
    if (spec.internalFormat)
        return spec.internalFormat;
    switch (spec.type)
    {
    case GpuTypeEnum::Byte:
    case GpuTypeEnum::UnsignedByte:
        switch (spec.components)
        {
        case 1: return GL_R8;
        case 2: return GL_RG8;
        case 3: return GL_RGB8;
        case 4: return GL_RGBA8;
        }
        break;
    case GpuTypeEnum::Short:
    case GpuTypeEnum::UnsignedShort:
        switch (spec.components)
        {
        case 1: return GL_R16;
        case 2: return GL_RG16;
        case 3: return GL_RGB16;
        case 4: return GL_RGBA16;
        }
        break;
    case GpuTypeEnum::Int:
        switch (spec.components)
        {
        case 1: return GL_R32I;
        case 2: return GL_RG32I;
        case 3: return GL_RGB32I;
        case 4: return GL_RGBA32I;
        }
        break;
    case GpuTypeEnum::UnsignedInt:
        switch (spec.components)
        {
        case 1: return GL_R32UI;
        case 2: return GL_RG32UI;
        case 3: return GL_RGB32UI;
        case 4: return GL_RGBA32UI;
        }
        break;
    case GpuTypeEnum::HalfFloat:
        switch (spec.components)
        {
        case 1: return GL_R16F;
        case 2: return GL_RG16F;
        case 3: return GL_RGB16F;
        case 4: return GL_RGBA16F;
        }
        break;
    case GpuTypeEnum::Float:
        switch (spec.components)
        {
        case 1: return GL_R32F;
        case 2: return GL_RG32F;
        case 3: return GL_RGB32F;
        case 4: return GL_RGBA32F;
        }
        break;
    }
    throw std::invalid_argument("cannot deduce texture internal format");
}

GLenum findFormat(const GpuTextureSpec &spec)
{
    switch (spec.components)
    {
    case 1: return GL_RED;
    case 2: return GL_RG;
    case 3: return GL_RGB;
    case 4: return GL_RGBA;
    default:
        throw std::invalid_argument("invalid texture components count");
    }
}

GpuTextureSpec::FilterMode magFilter(
    const GpuTextureSpec::FilterMode &filterMode)
{
    switch (filterMode)
    {
    case GpuTextureSpec::FilterMode::Nearest:
    case GpuTextureSpec::FilterMode::NearestMipmapNearest:
    case GpuTextureSpec::FilterMode::LinearMipmapNearest:
        return GpuTextureSpec::FilterMode::Nearest;
    case GpuTextureSpec::FilterMode::Linear:
    case GpuTextureSpec::FilterMode::LinearMipmapLinear:
    case GpuTextureSpec::FilterMode::NearestMipmapLinear:
        return GpuTextureSpec::FilterMode::Linear;
    default:
        throw std::invalid_argument("invalid texture filter mode "
            "(in conversion for magnification filter)");
    }
}

void enforceUsingMipMaps(GpuTextureSpec::FilterMode &filterMode)
{
    switch (filterMode)
    {
    case GpuTextureSpec::FilterMode::Nearest:
    case GpuTextureSpec::FilterMode::NearestMipmapNearest:
    case GpuTextureSpec::FilterMode::LinearMipmapNearest:
        filterMode = GpuTextureSpec::FilterMode::LinearMipmapNearest;
        break;
    case GpuTextureSpec::FilterMode::Linear:
    case GpuTextureSpec::FilterMode::LinearMipmapLinear:
    case GpuTextureSpec::FilterMode::NearestMipmapLinear:
        filterMode = GpuTextureSpec::FilterMode::LinearMipmapLinear;
        break;
    default:
        throw std::invalid_argument("invalid texture filter mode");
    }
}

bool gpuTypeInteger(GpuTypeEnum type)
{
    switch (type)
    {
    case GpuTypeEnum::Byte:
    case GpuTypeEnum::UnsignedByte:
    case GpuTypeEnum::Short:
    case GpuTypeEnum::UnsignedShort:
    case GpuTypeEnum::Int:
    case GpuTypeEnum::UnsignedInt:
        return true;
    default:
        return false;
    }
}

} // namespace

void Texture::load(ResourceInfo &info, vts::GpuTextureSpec &spec,
    const std::string &debugId)
{
    assert(spec.buffer.size() == spec.width * spec.height
           * spec.components * gpuTypeSize(spec.type)
           || spec.buffer.size() == 0);

    clear();
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, findInternalFormat(spec),
                 spec.width, spec.height, 0,
                 findFormat(spec), (GLenum)spec.type, spec.buffer.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        (GLenum)spec.filterMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
        (GLenum)magFilter(spec.filterMode));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
        (GLenum)spec.wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
        (GLenum)spec.wrapMode);

    if (GLAD_GL_EXT_texture_filter_anisotropic)
    {
        glTexParameterf(GL_TEXTURE_2D,
                        GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        maxAnisotropySamples);
    }

    switch (spec.filterMode)
    {
    case GpuTextureSpec::FilterMode::Nearest:
    case GpuTextureSpec::FilterMode::Linear:
        break;
    default:
        glGenerateMipmap(GL_TEXTURE_2D);
        break;
    }

    grayscale = spec.components == 1;
    setDebugId(debugId);
    CHECK_GL("load texture");
    info.ramMemoryCost += sizeof(*this);
    info.gpuMemoryCost += spec.buffer.size();
}

void Texture::setId(uint32 id)
{
    clear();
    this->id = id;
    this->grayscale = false;
}

uint32 Texture::getId() const
{
    return id;
}

bool Texture::getGrayscale() const
{
    return grayscale;
}

void RenderContext::loadTexture(ResourceInfo &info, GpuTextureSpec &spec,
    const std::string &debugId)
{
    OPTICK_EVENT();

    if (impl->options.enforceUsingMipMaps)
        enforceUsingMipMaps(spec.filterMode);

    auto r = std::make_shared<Texture>();
    r->load(info, spec, debugId);
    info.userData = r;

    if (impl->options.callGlFinishAfterUploadingData)
    {
        OPTICK_EVENT("glFinish");
        glFinish();
    }
}

Mesh::Mesh()
{}

void Mesh::clear()
{
    if (vbo)
        glDeleteBuffers(1, &vbo);
    if (vio)
        glDeleteBuffers(1, &vio);
    vbo = vio = 0;
}

Mesh::~Mesh()
{
    clear();
}

void Mesh::setDebugId(const std::string &id)
{
    this->debugId = id;
    setDebugLabel(GL_BUFFER, vbo, debugId);
    setDebugLabel(GL_BUFFER, vio, debugId);
}

void Mesh::bind()
{
    if (vbo)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        uint32 i = 0;
        for (const GpuMeshSpec::VertexAttribute &a : spec.attributes)
        {
            if (a.enable)
            {
                glEnableVertexAttribArray(i);
                if (gpuTypeInteger(a.type) && !a.normalized)
                {
                    glVertexAttribIPointer(i,
                        a.components, (GLenum)a.type,
                        a.stride, (void*)(intptr_t)a.offset);
                }
                else
                {
                    glVertexAttribPointer(i,
                        a.components, (GLenum)a.type,
                        a.normalized ? GL_TRUE : GL_FALSE,
                        a.stride, (void*)(intptr_t)a.offset);
                }
            }
            else
                glDisableVertexAttribArray(i);
            i++;
        }
    }

    if (vio)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vio);

    CHECK_GL("bind mesh");
}

void Mesh::dispatch()
{
    if (spec.indicesCount > 0)
        glDrawElements((GLenum)spec.faceMode, spec.indicesCount,
                       (GLenum)spec.indexMode, nullptr);
    else
        glDrawArrays((GLenum)spec.faceMode, 0, spec.verticesCount);
    CHECK_GL("dispatch mesh");
}

void Mesh::dispatch(uint32 offset, uint32 count)
{
    if (spec.indicesCount > 0)
        glDrawElements((GLenum)spec.faceMode, count, (GLenum)spec.indexMode,
            (void*)(std::size_t)(gpuTypeSize(spec.indexMode) * offset));
    else
        glDrawArrays((GLenum)spec.faceMode, offset, count);
    CHECK_GL("dispatch mesh");
}

void Mesh::dispatchWireframeSlow()
{
    assert((GLenum)spec.faceMode == GL_TRIANGLES);
    if (spec.indicesCount > 0)
    {
        for (uint32 i = 0; i < spec.indicesCount; i += 3)
        {
            glDrawElements(GL_LINE_LOOP, 3, (GLenum)spec.indexMode,
                (void*)(std::size_t)(gpuTypeSize(spec.indexMode) * i));
        }
    }
    else
    {
        for (uint32 i = 0; i < spec.verticesCount; i += 3)
        {
            glDrawArrays(GL_LINE_LOOP, i, 3);
        }
    }
}

void Mesh::load(ResourceInfo &info, GpuMeshSpec &specp,
    const std::string &debugId)
{
    clear();
    spec = std::move(specp);
    if (spec.verticesCount)
    {
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                 spec.vertices.size(), spec.vertices.data(), GL_STATIC_DRAW);
    }
    if (spec.indicesCount)
    {
        glGenBuffers(1, &vio);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vio);
        Buffer uint16_indices_buffer;
        uint16_indices_buffer.resize(spec.indicesCount * sizeof(uint16_t));
        auto uint16_indices = (uint16_t*)uint16_indices_buffer.data();
        auto uint32_indices = (uint32_t*)spec.indices.data();
        for (uint32_t i = 0; i < spec.indicesCount; i++) {
            uint16_indices[i] = uint32_indices[i];
        }
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     uint16_indices_buffer.size(), uint16_indices_buffer.data(), GL_STATIC_DRAW);
    }
    setDebugId(debugId);
    CHECK_GL("load mesh");
    info.ramMemoryCost += sizeof(*this);
    info.gpuMemoryCost += spec.vertices.size() + spec.indices.size();
    spec.vertices.free();
    spec.indices.free();
}

uint32 Mesh::getVbo() const
{
    return vbo;
}

uint32 Mesh::getVio() const
{
    return vio;
}

void RenderContext::loadMesh(ResourceInfo &info, GpuMeshSpec &spec,
    const std::string &debugId)
{
    OPTICK_EVENT();

    auto r = std::make_shared<Mesh>();
    r->load(info, spec, debugId);
    info.userData = r;

    if (impl->options.callGlFinishAfterUploadingData)
    {
        OPTICK_EVENT("glFinish");
        glFinish();
    }
}

UniformBuffer::UniformBuffer()
{}

UniformBuffer::~UniformBuffer()
{
    clear();
}

UniformBuffer::UniformBuffer(UniformBuffer &&other) noexcept
{
    std::swap(debugId, other.debugId);
    std::swap(ubo, other.ubo);
    std::swap(lastUsage, other.lastUsage);
    std::swap(capacity, other.capacity);
}

UniformBuffer &UniformBuffer::operator = (UniformBuffer &&other) noexcept
{
    std::swap(debugId, other.debugId);
    std::swap(ubo, other.ubo);
    std::swap(lastUsage, other.lastUsage);
    std::swap(capacity, other.capacity);
    return *this;
}

void UniformBuffer::setDebugId(const std::string &id)
{
    this->debugId = id;
    setDebugLabel(GL_BUFFER, ubo, debugId);
}

void UniformBuffer::clear()
{
    if (ubo)
        glDeleteBuffers(1, &ubo);
    ubo = 0;
    lastUsage = 0;
    capacity = 0;
}

void UniformBuffer::bindInit()
{
    if (ubo == 0)
    {
        glGenBuffers(1, &ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        setDebugId(debugId);
    }
}

void UniformBuffer::bind()
{
    bindInit();
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
}

void UniformBuffer::bindToIndex(uint32 index)
{
    bindInit();
    glBindBufferBase(GL_UNIFORM_BUFFER, index, ubo);
}

void UniformBuffer::load(const void *data, std::size_t size, uint32 usage)
{
    assert(ubo != 0);

    // not for reading
    assert(usage != GL_STREAM_READ);
    assert(usage != GL_STATIC_READ);
    assert(usage != GL_DYNAMIC_READ);

#ifdef VTSR_UWP
    // angle/directx seems to not like changing a buffer
    // create a new one instead
    clear();
    bind();
#endif // VTSR_UWP

    if (size <= capacity && usage == lastUsage)
    {
        // usage must be GL_DYNAMIC_*
        assert(usage != GL_STREAM_COPY);
        assert(usage != GL_STREAM_DRAW);
        assert(usage != GL_STATIC_COPY);
        assert(usage != GL_STATIC_DRAW);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data);
    }
    else
    {
        glBufferData(GL_UNIFORM_BUFFER, size, data, usage);
        lastUsage = usage;
        capacity = size;
    }
}

void UniformBuffer::load(const Buffer &buffer, uint32 usage)
{
    load(buffer.data(), buffer.size(), usage);
}

uint32 UniformBuffer::getUbo() const
{
    return ubo;
}

} } // namespace vts renderer

