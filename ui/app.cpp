// flam3fox - a small Apophysis-inspired interactive viewer/editor built on
// libflam3 via RenderEngine. This file is the GLFW + OpenGL + Dear ImGui glue;
// all the flame work lives in RenderEngine. Requires a display to run.
#include "render_engine.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void glfwErr(int e, const char *d) { fprintf(stderr, "GLFW %d: %s\n", e, d); }

int main(int argc, char **argv) {
    // libflam3 finds its palette file via $flam3_palettes; default to the
    // repo copy (the binary typically runs from ui/build) unless already set.
    if (!getenv("flam3_palettes"))
        setenv("flam3_palettes", "../../flam3-palettes.xml", 0);

    const char *startFlame = (argc > 1) ? argv[1] : "../../bench/scene.flam3";

    glfwSetErrorCallback(glfwErr);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW (no display?)\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *win = glfwCreateWindow(1280, 800, "flam3fox", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    RenderEngine engine;
    engine.loadFromFile(startFlame);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::vector<unsigned char> img;
    int texW = 0, texH = 0;
    uint64_t shownVersion = (uint64_t)-1;
    char pathBuf[512];
    std::snprintf(pathBuf, sizeof(pathBuf), "%s", startFlame);
    float targetQ = (float)engine.targetQuality();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Upload a freshly rendered frame to the GL texture when available.
        int iw = 0, ih = 0; uint64_t ver = 0;
        if (engine.latestImage(img, iw, ih, ver) && ver != shownVersion) {
            shownVersion = ver;
            glBindTexture(GL_TEXTURE_2D, tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, img.data());
            texW = iw; texH = ih;
        }

        // ---- Controls panel ------------------------------------------------
        ImGui::Begin("Flame");
        ImGui::InputText("file", pathBuf, sizeof(pathBuf));
        if (ImGui::Button("Load")) {
            if (!engine.loadFromFile(pathBuf))
                fprintf(stderr, "could not load %s\n", pathBuf);
        }

        ImGui::Separator();
        ImGui::Text("threads: %d", engine.threads());
        ImGui::Text("quality: %.0f / %.0f%s", engine.currentQuality(),
                    engine.targetQuality(), engine.isRendering() ? " (rendering)" : "");
        ImGui::Text("last level: %d ms", engine.lastRenderMs());
        if (ImGui::SliderFloat("target quality", &targetQ, 10.0f, 5000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic))
            engine.setTargetQuality(targetQ);

        flam3_genome *g = engine.genome();
        if (g) {
            ImGui::Separator();
            ImGui::Text("Camera");
            bool changed = false;
            changed |= ImGui::DragScalarN("center", ImGuiDataType_Double, g->center, 2, 0.005f);
            changed |= ImGui::DragScalar("zoom", ImGuiDataType_Double, &g->zoom, 0.01f);
            changed |= ImGui::DragScalar("rotate", ImGuiDataType_Double, &g->rotate, 0.5f);
            changed |= ImGui::DragScalar("scale (ppu)", ImGuiDataType_Double, &g->pixels_per_unit, 0.5f);

            ImGui::Separator();
            ImGui::Text("Tone");
            changed |= ImGui::DragScalar("brightness", ImGuiDataType_Double, &g->brightness, 0.02f);
            changed |= ImGui::DragScalar("gamma", ImGuiDataType_Double, &g->gamma, 0.02f);
            changed |= ImGui::DragScalar("vibrancy", ImGuiDataType_Double, &g->vibrancy, 0.01f);

            ImGui::Separator();
            ImGui::Text("Transforms: %d", g->num_xforms);
            for (int i = 0; i < g->num_xforms; i++) {
                ImGui::PushID(i);
                changed |= ImGui::DragScalar("weight", ImGuiDataType_Double,
                                             &g->xform[i].density, 0.005f);
                ImGui::SameLine();
                ImGui::Text("xform %d", i);
                ImGui::PopID();
            }

            if (changed)
                engine.requestRerender();
        }
        ImGui::End();

        // ---- Render view ---------------------------------------------------
        ImGui::Begin("Preview");
        if (texW > 0) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float s = avail.x / (float)texW;
            float sy = avail.y / (float)texH;
            if (sy < s) s = sy;
            if (s <= 0) s = 1.0f;
            ImGui::Image((ImTextureID)(intptr_t)tex,
                         ImVec2(texW * s, texH * s));
        } else {
            ImGui::Text("no image yet");
        }
        ImGui::End();

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    glDeleteTextures(1, &tex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
