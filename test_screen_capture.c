#if 0
clang -g -Wall -std=c99 -o test_screen_capture $0 \
    -framework OpenGL \
    -framework CoreGraphics -framework IOSurface -framework CoreFoundation -lglfw3 \
    && ./test_screen_capture;
exit;
#endif

#include <stdio.h>
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#define SAO_SCREEN_CAPTURE_IMPLEMENTATION
#include "sao_screen_capture.h"

int main()
{
    // @TODO: set up window
    glfwInit();

    GLFWwindow* window = glfwCreateWindow(640, 480, "test screen capture", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // set up capture queue
    sc_CaptureQueue* cq = sc_allocate();

    // set up texture to render capture.
    uint32_t capture_texture;
    glGenTextures(1, &capture_texture);
    glBindTexture(GL_TEXTURE_2D, capture_texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cq->capture_width, cq->capture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // start capture
    sc_startCapture(cq);

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        
        sc_Frame* frame = sc_aquireNextFrame(cq);

        if (frame) {
            printf("Got that frame\n");

            // copy to texture
            glBindTexture(GL_TEXTURE_2D, capture_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cq->capture_width, cq->capture_height,
                            GL_BGRA, GL_UNSIGNED_BYTE, frame->data);
            
            sc_releaseFrame(cq, frame);

            // render frame
            glClear(GL_COLOR_BUFFER_BIT);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0.0, width, 0.0, height, -1.0, 1.0);
            glViewport(0,0,width, height);
            glScalef(width, height, 0.0);
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();

            glLoadIdentity();
            glDisable(GL_LIGHTING);

            glColor3f(1,1,1);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, capture_texture);

            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex3f(0, 1, 0);
            glTexCoord2f(0, 1); glVertex3f(0, 0, 0);
            glTexCoord2f(1, 1); glVertex3f(1, 0, 0);
            glTexCoord2f(1, 0); glVertex3f(1, 1, 0);
            glEnd();

            glDisable(GL_TEXTURE_2D);
            glPopMatrix();


            glMatrixMode(GL_PROJECTION);
            glPopMatrix();

            glMatrixMode(GL_MODELVIEW);
            
            glfwSwapBuffers(window);
        }

        glfwPollEvents();
    }
    sc_stopCapture(cq);
    sc_free(cq);

    glfwDestroyWindow(window);
    glfwTerminate();
}
