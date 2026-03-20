#include "background.h"
#include "viewport_render.h"

#include <cmath>
#include <cstdlib>
#include <vector>

#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif
#ifndef GL_POINT_SMOOTH
#define GL_POINT_SMOOTH 0x0B10
#endif

GLuint gCheckerOverlayTex = 0;

void DrawViewportBackground(int themeMode, bool playMode)
{
    // Background gradient — fully reset GL state so vertex colors are not
    // modulated by leftover lighting / material state from the previous frame.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
    if (playMode)
    {
        glColor3f(0.0f, 0.0f, 0.0f);
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    else if (themeMode == 0)
    {
        glColor3f(0.031f, 0.035f, 0.047f); // bottom #08090C
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glColor3f(0.078f, 0.086f, 0.110f); // top #14161C
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    else if (themeMode == 1)
    {
        glColor3f(0.165f, 0.212f, 0.239f); // bottom #2A363D
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glColor3f(0.427f, 0.498f, 0.537f); // top #6D7F89
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    else if (themeMode == 2)
    {
        glColor3f(0.086f, 0.133f, 0.161f); // bottom #162229
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glColor3f(0.459f, 0.659f, 0.698f); // top #75A8B2
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    else if (themeMode == 3)
    {
        glColor3f(0.353f, 0.349f, 0.361f); // bottom #5A595C
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glColor3f(0.498f, 0.494f, 0.514f); // top #7F7E83
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    else
    {
        glColor3f(0.0f, 0.0f, 0.0f); // bottom #000000
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f( 1.0f, -1.0f, 0.0f);
        glColor3f(0.0f, 0.0f, 0.0f); // top #000000
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
    }
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);

    // Fog disabled for now (was hiding stars)
    glDisable(GL_FOG);

    // Procedural stars (world-space)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POINT_SMOOTH);

    if (gCheckerOverlayTex == 0)
    {
        const int texSize = 64;
        std::vector<unsigned char> pixels(texSize * texSize * 4);
        for (int y = 0; y < texSize; ++y)
        {
            for (int x = 0; x < texSize; ++x)
            {
                bool even = (((x >> 3) + (y >> 3)) & 1) == 0;
                int i = (y * texSize + x) * 4;
                if (even)
                {
                    // White checker cells are transparent
                    pixels[i + 0] = 220; pixels[i + 1] = 200; pixels[i + 2] = 255; pixels[i + 3] = 0;
                }
                else
                {
                    // Purple checker cells are visible
                    pixels[i + 0] = 120; pixels[i + 1] = 90; pixels[i + 2] = 235; pixels[i + 3] = 255;
                }
            }
        }

        glGenTextures(1, &gCheckerOverlayTex);
        glBindTexture(GL_TEXTURE_2D, gCheckerOverlayTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texSize, texSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glPointSize(2.0f);
    glBegin(GL_POINTS);
    static bool starsInit = false;
    static float stars[4000][3];
    static float starPhase[4000];
    static float starSpeed[4000];
    static float starTint[4000];
    static float starSize[4000];
    static float nebula[400][3];
    static float nebulaCol[400][3];
    if (!starsInit)
    {
        starsInit = true;
        for (int i = 0; i < 4000; ++i)
        {
            float u = (float)rand() / (float)RAND_MAX;
            float v = (float)rand() / (float)RAND_MAX;
            float theta = u * 6.28318f;
            float phi = acosf(2.0f * v - 1.0f);
            float r = 500.0f;
            stars[i][0] = r * sinf(phi) * cosf(theta);
            stars[i][1] = r * cosf(phi);
            stars[i][2] = r * sinf(phi) * sinf(theta);
            starPhase[i] = (float)rand() / (float)RAND_MAX * 6.28318f;
            starSpeed[i] = 0.5f + (float)rand() / (float)RAND_MAX * 1.5f;
            starTint[i] = (float)rand() / (float)RAND_MAX; // 0..1
            starSize[i] = 1.0f + (float)rand() / (float)RAND_MAX * 999.0f;
        }
        for (int i = 0; i < 400; ++i)
        {
            float u = (float)rand() / (float)RAND_MAX;
            float v = (float)rand() / (float)RAND_MAX;
            float theta = u * 6.28318f;
            float phi = acosf(2.0f * v - 1.0f);
            float r = 480.0f;
            nebula[i][0] = r * sinf(phi) * cosf(theta);
            nebula[i][1] = r * cosf(phi);
            nebula[i][2] = r * sinf(phi) * sinf(theta);
            nebulaCol[i][0] = 0.20f + 0.40f * ((float)rand() / (float)RAND_MAX);
            nebulaCol[i][1] = 0.15f + 0.30f * ((float)rand() / (float)RAND_MAX);
            nebulaCol[i][2] = 0.30f + 0.45f * ((float)rand() / (float)RAND_MAX);
        }

        // (reverted)
    }
    double t = glfwGetTime();

    if (themeMode == 0)
    {
        // Nebula (mid, world-space)
        glPointSize(10.0f);
        for (int i = 0; i < 400; ++i)
        {
            glColor4f(nebulaCol[i][0], nebulaCol[i][1], nebulaCol[i][2], 0.6f);
            glVertex3f(nebula[i][0], nebula[i][1], nebula[i][2]);
        }

        // Stars (front) — GL_POINTS buckets (3/6/12px)
        auto drawPoints = [&](float sizeMin, float sizeMax, float px)
        {
            glPointSize(px);
            glBegin(GL_POINTS);
            for (int i = 0; i < 4000; ++i)
            {
                if (starSize[i] < sizeMin || starSize[i] > sizeMax) continue;
                float b = 0.3f + 0.7f * (0.5f + 0.5f * sinf((float)t * starSpeed[i] + starPhase[i]));
                if (starTint[i] < 0.5f) glColor4f(b, b, b, b);
                else glColor4f(0.6f * b, 0.7f * b, 1.0f * b, b);
                glVertex3f(stars[i][0], stars[i][1], stars[i][2]);
            }
            glEnd();
        };

        drawPoints(0.0f, 300.0f, 3.0f);
        drawPoints(300.0f, 700.0f, 6.0f);
        drawPoints(700.0f, 10000.0f, 10.0f);
    }

    // (reverted)

    // Grid
    const int gridSize = 10;
    const float spacing = 0.5f;
    float half = gridSize * spacing;

    glColor3f(0.6f, 0.6f, 0.6f);
    glBegin(GL_LINES);
    for (int i = -gridSize; i <= gridSize; ++i)
    {
        if (i == 0) continue; // skip axes so colors stay clean
        float x = i * spacing;
        glVertex3f(x, 0.0f, -half);
        glVertex3f(x, 0.0f, half);

        float z = i * spacing;
        glVertex3f(-half, 0.0f, z);
        glVertex3f(half, 0.0f, z);
    }
    glEnd();

    // Axes
    glBegin(GL_LINES);
    glColor3f(0.8f, 0.2f, 0.2f);
    glVertex3f(-half, 0.001f, 0.0f);
    glVertex3f(half, 0.001f, 0.0f);

    // amber Z axis
    glColor3f(0.95f, 0.65f, 0.1f);
    glVertex3f(0.0f, 0.001f, -half);
    glVertex3f(0.0f, 0.001f, half);

    // border removed
    glEnd();
}
