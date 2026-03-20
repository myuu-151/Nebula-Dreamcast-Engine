#include "node_gizmos.h"
#include "viewport_render.h"
#include "viewport_transform.h"
#include "node_helpers.h"
#include "../editor/editor_state.h"

#include <cmath>
#include <algorithm>

void DrawNodeGizmos(const Camera3DNode* activeCam)
{
    // Audio3D visual indicators (wire sphere + vertical line)
    for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
    {
        const auto& a = gAudio3DNodes[i];
        bool selected = (gSelectedAudio3D == i);
        if (gHideUnselectedWireframes && !selected) continue;

        float rOuter = a.outerRadius;
        float rInner = a.innerRadius;
        if (rOuter < rInner) std::swap(rOuter, rInner);
        float scaleAvg = (a.scaleX + a.scaleY + a.scaleZ) / 3.0f;
        if (scaleAvg < 0.01f) scaleAvg = 0.01f;
        if (selected) glColor3f(1.0f, 1.0f, 1.0f);
        else glColor3f(0.2f, 0.6f, 1.0f);

        // Vertical marker
        glBegin(GL_LINES);
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(a.x, a.y + 1.0f, a.z);
        glEnd();

        const int segments = 16;
        const int rings = 12;

        auto drawSphere = [&](float r, float cr, float cg, float cb)
        {
            if (selected) glColor3f(1.0f, 1.0f, 1.0f);
            else glColor3f(cr, cg, cb);
            // Latitude rings
            for (int j = 1; j < rings; ++j)
            {
                float v = (float)j / (float)rings;
                float phi = v * 3.1415926f;
                float y = cosf(phi) * r;
                float rr = sinf(phi) * r;

                glBegin(GL_LINE_LOOP);
                for (int i = 0; i < segments; ++i)
                {
                    float t = (float)i / (float)segments * 6.2831853f;
                    float x = cosf(t) * rr;
                    float z = sinf(t) * rr;
                    glVertex3f(x, y, z);
                }
                glEnd();
            }

            // Longitude rings
            for (int i = 0; i < segments; ++i)
            {
                float t = (float)i / (float)segments * 6.2831853f;
                float cx = cosf(t);
                float cz = sinf(t);

                glBegin(GL_LINE_LOOP);
                for (int j = 0; j <= rings; ++j)
                {
                    float v = (float)j / (float)rings;
                    float phi = v * 3.1415926f;
                    float y = cosf(phi) * r;
                    float rr = sinf(phi) * r;
                    float x = cx * rr;
                    float z = cz * rr;
                    glVertex3f(x, y, z);
                }
                glEnd();
            }
        };

        glPushMatrix();
        glTranslatef(a.x, a.y, a.z);
        float drawRotX = a.rotX;
        float drawRotY = a.rotY;
        float drawRotZ = a.rotZ;
        if (gTransformMode == Transform_Rotate && gHasRotatePreview && gRotatePreviewIndex == i)
        {
            drawRotX = gRotatePreviewX;
            drawRotY = gRotatePreviewY;
            drawRotZ = gRotatePreviewZ;
        }
        glRotatef(drawRotX, 1.0f, 0.0f, 0.0f);
        glRotatef(drawRotY, 0.0f, 1.0f, 0.0f);
        glRotatef(drawRotZ, 0.0f, 0.0f, 1.0f);

        drawSphere(rOuter, 0.2f, 0.6f, 1.0f);
        drawSphere(rInner, 0.1f, 1.0f, 0.4f);
        drawSphere(0.25f * scaleAvg, 1.0f, 0.9f, 0.2f); // node core

        // Rotation axes (visual)
        Vec3 rAxis, uAxis, fAxis;
        GetLocalAxes(a, rAxis, uAxis, fAxis);
        float axisLen = 0.6f * scaleAvg;
        glBegin(GL_LINES);
        glColor3f(1.0f, 0.2f, 0.2f); // X
        glVertex3f(0.0f, 0.0f, 0.0f);
        glVertex3f(rAxis.x * axisLen, rAxis.y * axisLen, rAxis.z * axisLen);
        glColor3f(0.2f, 1.0f, 0.2f); // Y
        glVertex3f(0.0f, 0.0f, 0.0f);
        glVertex3f(uAxis.x * axisLen, uAxis.y * axisLen, uAxis.z * axisLen);
        glColor3f(0.2f, 0.4f, 1.0f); // Z
        glVertex3f(0.0f, 0.0f, 0.0f);
        glVertex3f(fAxis.x * axisLen, fAxis.y * axisLen, fAxis.z * axisLen);
        glEnd();

        glPopMatrix();

        // Infinite axis line for G/R + axis lock (world axis)
        if ((gTransformMode == Transform_Grab || gTransformMode == Transform_Rotate) && gAxisLock && gSelectedAudio3D == i)
        {
            Vec3 axis = { 1.0f, 0.0f, 0.0f };
            if (gAxisLock == 'Y') axis = { 0.0f, 1.0f, 0.0f };
            if (gAxisLock == 'Z') axis = { 0.0f, 0.0f, 1.0f };

            float len = 2000.0f;
            glLineWidth(2.5f);
            glBegin(GL_LINES);
            if (gAxisLock == 'X') glColor3f(1.0f, 0.4f, 0.4f);
            else if (gAxisLock == 'Y') glColor3f(0.4f, 1.0f, 0.4f);
            else glColor3f(0.4f, 0.6f, 1.0f);
            glVertex3f(a.x - axis.x * len, a.y - axis.y * len, a.z - axis.z * len);
            glVertex3f(a.x + axis.x * len, a.y + axis.y * len, a.z + axis.z * len);
            glEnd();
            glLineWidth(1.0f);
        }
    }

    // Camera3D visual helpers (original fallback marker)
    for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
    {
        const auto& c = gCamera3DNodes[i];
        bool selected = (gSelectedCamera3D == i);
        if (gHideUnselectedWireframes && !selected) continue;
        // Don't draw helper for the camera currently driving the play viewport.
        if (gPlayMode && activeCam == &gCamera3DNodes[i]) continue;

        glDisable(GL_TEXTURE_2D);
        if (selected) glColor3f(1.0f, 1.0f, 1.0f);
        else if (c.main) glColor3f(0.2f, 1.0f, 0.4f);
        else glColor3f(0.1f, 0.8f, 0.3f);

        float cwx, cwy, cwz, cwrx, cwry, cwrz;
        GetCamera3DWorldTR(i, cwx, cwy, cwz, cwrx, cwry, cwrz);
        glPushMatrix();
        glTranslatef(cwx, cwy, cwz);
        glRotatef(cwrx, 1.0f, 0.0f, 0.0f);
        glRotatef(cwry, 0.0f, 1.0f, 0.0f);
        glRotatef(cwrz, 0.0f, 0.0f, 1.0f);

        // original helper marker
        glBegin(GL_LINES);
        glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.0f, 0.0f, 1.0f);
        glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.3f, 0.2f, 0.7f);
        glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(-0.3f, 0.2f, 0.7f);
        glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.3f, -0.2f, 0.7f);
        glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(-0.3f, -0.2f, 0.7f);
        glEnd();

        glPopMatrix();
    }

    // Node3D rendering (uses primitive mesh, defaults to cube_primitive)
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
    {
        const auto& n = gNode3DNodes[i];
        const bool selected = (gSelectedNode3D == i);
        if (selected) glColor3f(1.0f, 1.0f, 1.0f);
        else glColor3f(0.55f, 0.9f, 1.0f);

        float wx, wy, wz, wqw, wqx, wqy, wqz, wsx, wsy, wsz;
        GetNode3DWorldTRSQuat(i, wx, wy, wz, wqw, wqx, wqy, wqz, wsx, wsy, wsz);
        glPushMatrix();
        glTranslatef(wx, wy, wz);
        { float rm[16]; QuatToGLMatrix(wqw, wqx, wqy, wqz, rm); glMultMatrixf(rm); }
        // Apply local bounds offset (collision-only, does not affect parent/child transforms).
        glTranslatef(n.boundPosX, n.boundPosY, n.boundPosZ);
        // Apply local collision extents to Node3D box size, without affecting hierarchy inheritance.
        const float ex = std::max(0.0f, n.extentX * 2.0f);
        const float ey = std::max(0.0f, n.extentY * 2.0f);
        const float ez = std::max(0.0f, n.extentZ * 2.0f);
        glScalef(wsx * ex, wsy * ey, wsz * ez);

        const float q = 0.5f;
        glBegin(GL_QUADS);
        // Front
        glVertex3f(-q, -q,  q); glVertex3f( q, -q,  q); glVertex3f( q,  q,  q); glVertex3f(-q,  q,  q);
        // Back
        glVertex3f( q, -q, -q); glVertex3f(-q, -q, -q); glVertex3f(-q,  q, -q); glVertex3f( q,  q, -q);
        // Left
        glVertex3f(-q, -q, -q); glVertex3f(-q, -q,  q); glVertex3f(-q,  q,  q); glVertex3f(-q,  q, -q);
        // Right
        glVertex3f( q, -q,  q); glVertex3f( q, -q, -q); glVertex3f( q,  q, -q); glVertex3f( q,  q,  q);
        // Top
        glVertex3f(-q,  q,  q); glVertex3f( q,  q,  q); glVertex3f( q,  q, -q); glVertex3f(-q,  q, -q);
        // Bottom
        glVertex3f(-q, -q, -q); glVertex3f( q, -q, -q); glVertex3f( q, -q,  q); glVertex3f(-q, -q,  q);
        glEnd();
        glPopMatrix();
    }

    // NavMesh3D bounds rendering (wireframe box, cyan for positive, red for negator)
    for (int i = 0; i < (int)gNavMesh3DNodes.size(); ++i)
    {
        const auto& n = gNavMesh3DNodes[i];
        if (!n.navBounds) continue;
        const bool selected = (gSelectedNavMesh3D == i);
        if (selected) glColor3f(1.0f, 1.0f, 1.0f);
        else if (n.navNegator) glColor3f(1.0f, 0.25f, 0.25f);
        else glColor3f(n.wireR, n.wireG, n.wireB);

        glLineWidth(n.wireThickness);
        glPushMatrix();
        glTranslatef(n.x, n.y, n.z);
        glRotatef(n.rotX, 1.0f, 0.0f, 0.0f);
        glRotatef(n.rotY, 0.0f, 1.0f, 0.0f);
        glRotatef(n.rotZ, 0.0f, 0.0f, 1.0f);
        glScalef(n.scaleX * n.extentX, n.scaleY * n.extentY, n.scaleZ * n.extentZ);

        const float q = 0.5f;
        glBegin(GL_QUADS);
        glVertex3f(-q, -q,  q); glVertex3f( q, -q,  q); glVertex3f( q,  q,  q); glVertex3f(-q,  q,  q);
        glVertex3f( q, -q, -q); glVertex3f(-q, -q, -q); glVertex3f(-q,  q, -q); glVertex3f( q,  q, -q);
        glVertex3f(-q, -q, -q); glVertex3f(-q, -q,  q); glVertex3f(-q,  q,  q); glVertex3f(-q,  q, -q);
        glVertex3f( q, -q,  q); glVertex3f( q, -q, -q); glVertex3f( q,  q, -q); glVertex3f( q,  q,  q);
        glVertex3f(-q,  q,  q); glVertex3f( q,  q,  q); glVertex3f( q,  q, -q); glVertex3f(-q,  q, -q);
        glVertex3f(-q, -q, -q); glVertex3f( q, -q, -q); glVertex3f( q, -q,  q); glVertex3f(-q, -q,  q);
        glEnd();
        glPopMatrix();
        glLineWidth(1.0f);
    }
}
