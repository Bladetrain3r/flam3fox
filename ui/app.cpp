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
    char savePath[512] = "out.png";
    char flamePath[512] = "out.flam3";
    float targetQ = (float)engine.targetQuality();
    RandomParams rp;

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
        bool simd = engine.simd();
        if (ImGui::Checkbox("SIMD preview (AVX2)", &simd))
            engine.setSimd(simd);
        ImGui::SameLine();
        ImGui::TextDisabled("(scalar fallback if unsupported)");

        ImGui::Separator();
        ImGui::InputText("png out", savePath, sizeof(savePath));
        if (ImGui::Button("Save PNG")) {
            if (!engine.savePNG(savePath))
                fprintf(stderr, "save failed (no image yet?): %s\n", savePath);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(current preview)");

        ImGui::InputText("flam3 out", flamePath, sizeof(flamePath));
        if (ImGui::Button("Save .flam3")) {
            if (!engine.saveFlam3(flamePath))
                fprintf(stderr, "save failed: %s\n", flamePath);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(parameters)");

        // ---- Random scene ---------------------------------------------------
        if (ImGui::CollapsingHeader("Random scene")) {
            ImGui::InputInt("min xforms", &rp.minXforms);
            ImGui::InputInt("max xforms", &rp.maxXforms);
            ImGui::InputInt("symmetry", &rp.symmetry);
            ImGui::InputInt("size", &rp.size);
            ImGui::InputDouble("fit min", &rp.zoomFitMin, 0.05, 0.1, "%.2f");
            ImGui::InputDouble("fit max", &rp.zoomFitMax, 0.05, 0.1, "%.2f");
            ImGui::Checkbox("SIMD-fast variations only", &rp.fastVarsOnly);
            if (ImGui::Button("Randomize"))
                engine.randomize(rp);
        }

        // Numeric fields below are click-to-type (single click to edit).
        flam3_genome *g = engine.genome();
        if (g) {
            ImGui::Separator();
            ImGui::Text("Camera");
            bool changed = false;
            changed |= ImGui::InputScalarN("center", ImGuiDataType_Double, g->center, 2, nullptr, nullptr, "%.5f");
            changed |= ImGui::InputDouble("zoom", &g->zoom, 0.1, 0.5, "%.3f");
            changed |= ImGui::InputDouble("rotate", &g->rotate, 1.0, 15.0, "%.2f");
            changed |= ImGui::InputDouble("scale (ppu)", &g->pixels_per_unit, 1.0, 10.0, "%.2f");

            ImGui::Separator();
            ImGui::Text("Tone");
            changed |= ImGui::InputDouble("brightness", &g->brightness, 0.1, 1.0, "%.3f");
            changed |= ImGui::InputDouble("gamma", &g->gamma, 0.1, 1.0, "%.3f");
            changed |= ImGui::InputDouble("vibrancy", &g->vibrancy, 0.05, 0.25, "%.3f");

            ImGui::Separator();
            ImGui::Text("Transforms: %d", g->num_xforms);
            for (int i = 0; i < g->num_xforms; i++) {
                ImGui::PushID(i);
                char lbl[32];
                std::snprintf(lbl, sizeof(lbl), "weight xform %d", i);
                changed |= ImGui::InputDouble(lbl, &g->xform[i].density, 0.01, 0.1, "%.4f");
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
