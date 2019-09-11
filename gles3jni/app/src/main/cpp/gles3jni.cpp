/*
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gles3jni.h"

const Vertex QUAD[4] = {
        // Square with diagonal < 2 so that it fits in a [-1 .. 1]^2 square
        // regardless of rotation.
        {{-0.01f, -0.01f}, {0x00, 0x00, 0xff}},
        {{0.01f,  -0.01f}, {0x00, 0x00, 0xff}},
        {{-0.01f, 0.01f},  {0xFF, 0x00, 0x00}},
        {{0.01f,  0.01f},  {0xFF, 0x00, 0x00}},
};

bool checkGlError(const char *funcName) {
    GLint err = glGetError();
    if (err != GL_NO_ERROR) {
        ALOGE("GL error after %s(): 0x%08x\n", funcName, err);
        return true;
    }
    return false;
}

GLuint createShader(GLenum shaderType, const char *src) {
    GLuint shader = glCreateShader(shaderType);
    if (!shader) {
        checkGlError("glCreateShader");
        return 0;
    }
    glShaderSource(shader, 1, &src, NULL);

    GLint compiled = GL_FALSE;
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLogLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLen);
        if (infoLogLen > 0) {
            GLchar *infoLog = (GLchar *) malloc(infoLogLen);
            if (infoLog) {
                glGetShaderInfoLog(shader, infoLogLen, NULL, infoLog);
                ALOGE("Could not compile %s shader:\n%s\n",
                      shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment",
                      infoLog);
                free(infoLog);
            }
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint createProgram(const char *vtxSrc, const char *fragSrc) {
    GLuint vtxShader = 0;
    GLuint fragShader = 0;
    GLuint program = 0;
    GLint linked = GL_FALSE;

    vtxShader = createShader(GL_VERTEX_SHADER, vtxSrc);
    if (!vtxShader)
        goto exit;

    fragShader = createShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!fragShader)
        goto exit;

    program = glCreateProgram();
    if (!program) {
        checkGlError("glCreateProgram");
        goto exit;
    }
    glAttachShader(program, vtxShader);
    glAttachShader(program, fragShader);

    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        ALOGE("Could not link program");
        GLint infoLogLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLen);
        if (infoLogLen) {
            GLchar *infoLog = (GLchar *) malloc(infoLogLen);
            if (infoLog) {
                glGetProgramInfoLog(program, infoLogLen, NULL, infoLog);
                ALOGE("Could not link program:\n%s\n", infoLog);
                free(infoLog);
            }
        }
        glDeleteProgram(program);
        program = 0;
    }

    exit:
    glDeleteShader(vtxShader);
    glDeleteShader(fragShader);
    return program;
}

static void printGlString(const char *name, GLenum s) {
    const char *v = (const char *) glGetString(s);
    ALOGV("GL %s: %s\n", name, v);
}

// ----------------------------------------------------------------------------

Renderer::Renderer()
        : mNumInstances(0),
          mLastFrameNs(0) {
    memset(mScale, 0, sizeof(mScale));
    memset(mAngularVelocity, 0, sizeof(mAngularVelocity));
    memset(mAngles, 0, sizeof(mAngles));
}

Renderer::~Renderer() {
}

void Renderer::resize(int w, int h) {
    auto offsets = mapOffsetBuf();
    calcSceneParams(w, h, offsets);
    unmapOffsetBuf();

    // Auto gives a signed int :-(
    for (auto i = (unsigned) 0; i < mNumInstances; i++) {
        mAngles[i] = drand48() * TWO_PI;
        mAngularVelocity[i] = MAX_ROT_SPEED * (2.0 * drand48() - 1.0);
    }

    mLastFrameNs = 0;

    glViewport(0, 0, w, h);
}

void Renderer::calcSceneParams(unsigned int w, unsigned int h,
                               float *offsets) {
    mNumInstances = MAX_INSTANCES_ITEM;
    localOffset = new float[MAX_INSTANCES_ITEM * 2];
    mVx = new float[MAX_INSTANCES_ITEM * 2];
    mVy = new float[MAX_INSTANCES_ITEM * 2];
    offsetRatio = 2.0f;
    for (int i = 0; i < MAX_INSTANCES_ITEM * 2; ++i) {
        localOffset[i] = (drand48() - 0.5) * offsetRatio;
        mVx[i] = (drand48() - 0.5) * 10.0;
        mVy[i] = (drand48() - 0.5) * 10.0;
        offsets[i] = localOffset[i];
    }
    float ratio = 0.1f;
    mScale[0] = ratio;
    mScale[1] = ratio * h / w;
}

void Renderer::step() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    auto nowNs = now.tv_sec * 1000000000ull + now.tv_nsec;

    if (mLastFrameNs > 0) {
        float dt = float(nowNs - mLastFrameNs) * 0.000000001f;
        __android_log_print(ANDROID_LOG_INFO, "step", "fps:%f", 1/dt);
//        for (unsigned int i = 0; i < mNumInstances; i++) {
//            mAngles[i] += mAngularVelocity[i] * dt;
//            if (mAngles[i] >= TWO_PI) {
//                mAngles[i] -= TWO_PI;
//            } else if (mAngles[i] <= -TWO_PI) {
//                mAngles[i] += TWO_PI;
//            }
//        }

//        float *transforms = mapTransformBuf();
//        for (unsigned int i = 0; i < mNumInstances; i++) {
//            float s = sinf(mAngles[i]);
//            float c = cosf(mAngles[i]);
//            transforms[4 * i + 0] = c * mScale[0];
//            transforms[4 * i + 1] = s * mScale[1];
//            transforms[4 * i + 2] = -s * mScale[0];
//            transforms[4 * i + 3] = c * mScale[1];
//        }
//        unmapTransformBuf();

        auto offsets = mapOffsetBuf();
        for (int i = 0; i < 2 * MAX_INSTANCES_ITEM - 1; i += 2) {
            localOffset[i] += mVx[i] * dt * 0.1;
            if (localOffset[i] > 1.0 || localOffset[i] < -1.0) {
                localOffset[i] -= 2 * mVx[i] * dt * 0.1;
                mVx[i] = -mVx[i];
            }
            offsets[i] = localOffset[i];

            localOffset[i + 1] += mVy[i] * dt * 0.1;
            if (localOffset[i + 1] > 1.0 || localOffset[i + 1] < -1.0) {
                localOffset[i + 1] -= 2 * mVy[i] * dt * 0.1;
                mVy[i] = -mVy[i];
            }
            offsets[i + 1] = localOffset[i + 1];
//        __android_log_print(ANDROID_LOG_INFO, "mgl","step %d offset %f", i, offsets[i]);

        };
        unmapOffsetBuf();
    }


    mLastFrameNs = nowNs;
}

void Renderer::render() {
    step();

    glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw(mNumInstances);
    checkGlError("Renderer::render");
}

// ----------------------------------------------------------------------------

static Renderer *g_renderer = NULL;

extern "C" {
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_init(JNIEnv *env, jobject obj);
JNIEXPORT void JNICALL
Java_com_android_gles3jni_GLES3JNILib_resize(JNIEnv *env, jobject obj, jint width, jint height);
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_step(JNIEnv *env, jobject obj);
};

#if !defined(DYNAMIC_ES3)

static GLboolean gl3stubInit() {
    return GL_TRUE;
}

#endif

JNIEXPORT void JNICALL
Java_com_android_gles3jni_GLES3JNILib_init(JNIEnv *env, jobject obj) {
    if (g_renderer) {
        delete g_renderer;
        g_renderer = NULL;
    }

    printGlString("Version", GL_VERSION);
    printGlString("Vendor", GL_VENDOR);
    printGlString("Renderer", GL_RENDERER);
    printGlString("Extensions", GL_EXTENSIONS);

    const char *versionStr = (const char *) glGetString(GL_VERSION);
    if (strstr(versionStr, "OpenGL ES 3.") && gl3stubInit()) {
        g_renderer = createES3Renderer();
    } else if (strstr(versionStr, "OpenGL ES 2.")) {
        g_renderer = createES2Renderer();
    } else {
        ALOGE("Unsupported OpenGL ES version");
    }
}

JNIEXPORT void JNICALL
Java_com_android_gles3jni_GLES3JNILib_resize(JNIEnv *env, jobject obj, jint width, jint height) {
    if (g_renderer) {
        g_renderer->resize(width, height);
    }
}

JNIEXPORT void JNICALL
Java_com_android_gles3jni_GLES3JNILib_step(JNIEnv *env, jobject obj) {
    if (g_renderer) {
        g_renderer->render();
    }
}
