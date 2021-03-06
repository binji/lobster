// Copyright 2014 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stdafx.h"

#include "glinterface.h"

#include "glincludes.h"

#include "sdlincludes.h"

#if !defined(PLATFORM_ES2) && !defined(__APPLE__)
#define GLEXT(type, name, needed) type name = nullptr;
GLBASEEXTS GLEXTS
#undef GLEXT
#endif

float4 curcolor = float4_0;

float4x4 view2clip(1);
objecttransforms otransforms;

vector<Light> lights;

float pointscale = 1.0f;
float custompointscale = 1.0f;

void AppendTransform(const float4x4 &forward, const float4x4 &backward)
{
    otransforms.object2view *= forward;
    otransforms.view2object = backward * otransforms.view2object;
}

int SetBlendMode(BlendMode mode)
{
    static BlendMode curblendmode = BLEND_NONE;
    if (mode == curblendmode) return curblendmode;
    switch (mode)
    {
        case BLEND_NONE:     glDisable(GL_BLEND); break;
        case BLEND_ALPHA:    glEnable (GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break; // alpha / interpolative
        case BLEND_ADD:      glEnable (GL_BLEND); glBlendFunc(GL_ONE,       GL_ONE                ); break; // additive (plain)
        case BLEND_ADDALPHA: glEnable (GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE                ); break; // additive (using src alpha)
        case BLEND_MUL:      glEnable (GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO               ); break; // multiplicative / masking
    }
    int old = curblendmode;
    curblendmode = mode;
    return old;
}

void ClearFrameBuffer(const float3 &c)
{
    glClearColor(c.x(), c.y(), c.z(), 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Set2DMode(const int2 &screensize)
{
    glDisable(GL_CULL_FACE);  
    glDisable(GL_DEPTH_TEST);

    otransforms = objecttransforms();

    view2clip = orthoGL(0, (float)screensize.x(), (float)screensize.y(), 0, 1, -1); // left handed coordinate system
}

void Set3DOrtho(const float3 &center, const float3 &extends)
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);  

    otransforms = objecttransforms();

    auto p = center + extends;
    auto m = center - extends;

    view2clip = orthoGL(m.x(), p.x(), p.y(), m.y(), m.z(), p.z()); // left handed coordinate system
}

void Set3DMode(float fovy, float ratio, float znear, float zfar)
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);  

    otransforms = objecttransforms();

    view2clip = perspectiveFov(fovy, ratio, znear, zfar, 1);
    view2clip *= float4x4(float4(1, -1, 1, 1)); // FIXME?
}

uchar *ReadPixels(const int2 &pos, const int2 &size, bool alpha)
{
    uchar *pixels = new uchar[(3 + (int)alpha) * size.x() * size.y()];
    glReadPixels(pos.x(), pos.y(), size.x(), size.y(), alpha ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixels);
    return pixels;
}

void OpenGLFrameStart(const int2 &screensize)
{
    glViewport(0, 0, screensize.x(), screensize.y());

    SetBlendMode(BLEND_ALPHA);

    curcolor = float4(1);

    lights.clear();
}

void OpenGLInit()
{
    #ifndef PLATFORM_ES2
    //auto vers = (char *)glGetString(GL_VERSION);
    auto exts = (char *)glGetString(GL_EXTENSIONS);
    if (exts)  // GL 4.x doesn't have this.
    {
        if (!strstr(exts, "GL_ARB_vertex_buffer_object")) throw string("no VBOs!");
        if (!strstr(exts, "GL_ARB_multitexture")) throw string("no multitexture!");
        if (!strstr(exts, "GL_ARB_vertex_program") || !strstr(exts, "GL_ARB_fragment_program")) throw string("no shaders!");
        //if (!strstr(exts, "GL_ARB_shading_language_100") || 
        //    !strstr(exts, "GL_ARB_shader_objects") ||
        //    !strstr(exts, "GL_ARB_vertex_shader") ||
        //    !strstr(exts, "GL_ARB_fragment_shader")) throw string("no GLSL!");
    }
    #endif

    #if !defined(PLATFORM_ES2) && !defined(__APPLE__)
    #define GLEXT(type, name, needed) \
        { \
            union { void *proc; type fun; } funcast; /* regular cast causes gcc warning */ \
            funcast.proc = SDL_GL_GetProcAddress(#name); \
            name = funcast.fun; \
            if (!name && needed) throw string("no " #name); \
        }
    GLBASEEXTS GLEXTS
    #undef GLEXT
    #endif

    #ifndef PLATFORM_ES2
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_MULTISAMPLE);
    #endif
}

