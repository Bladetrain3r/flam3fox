// Headless smoke test for RenderEngine: loads a flame, lets the progressive
// worker run to the target quality, and writes a PPM. No GUI/GL involved, so
// it runs anywhere (CI, headless containers) and exercises the libflam3
// orchestration path the GUI depends on.
#include "render_engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    const char *scene = (argc > 1) ? argv[1] : "../bench/scene.flam3";
    const char *outp  = (argc > 2) ? argv[2] : "engine_test.ppm";
    const double target = (argc > 3) ? atof(argv[3]) : 100.0;

    RenderEngine eng;
    eng.setTargetQuality(target);
    if (!eng.loadFromFile(scene)) {
        fprintf(stderr, "FAIL: could not load %s\n", scene);
        return 1;
    }
    fprintf(stderr, "loaded %s (threads=%d, target quality=%.0f)\n",
            scene, eng.threads(), target);

    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    uint64_t ver = 0, lastver = 0;

    for (int i = 0; i < 400; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (eng.latestImage(rgba, w, h, ver) && ver != lastver) {
            lastver = ver;
            fprintf(stderr, "  progressive: v%llu %dx%d quality=%.0f (%dms)\n",
                    (unsigned long long)ver, w, h,
                    eng.currentQuality(), eng.lastRenderMs());
        }
        if (!eng.isRendering() && eng.currentQuality() >= eng.targetQuality())
            break;
    }

    if (w <= 0) {
        fprintf(stderr, "FAIL: no image produced\n");
        return 1;
    }

    long nonblack = 0;
    double sum = 0.0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        int lum = rgba[i] + rgba[i + 1] + rgba[i + 2];
        if (lum > 0) nonblack++;
        sum += lum;
    }
    fprintf(stderr, "final %dx%d nonblack=%ld meanLum=%.3f\n",
            w, h, nonblack, sum / ((double)w * h * 3));

    FILE *fp = fopen(outp, "wb");
    if (fp) {
        fprintf(fp, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++)
            fwrite(&rgba[(size_t)i * 4], 1, 3, fp);
        fclose(fp);
        fprintf(stderr, "wrote %s\n", outp);
    }

    if (nonblack == 0) {
        fprintf(stderr, "FAIL: image is entirely black\n");
        return 2;
    }

    // Exercise the SIMD preview path on the loaded scene.
    eng.setSimd(true);
    eng.setTargetQuality(100);
    lastver = ver;
    for (int i = 0; i < 400; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (eng.latestImage(rgba, w, h, ver) && ver != lastver) {
            lastver = ver;
            if (!eng.isRendering() && eng.currentQuality() >= eng.targetQuality())
                break;
        }
    }
    {
        long nb = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
            if (rgba[i] + rgba[i + 1] + rgba[i + 2] > 0) nb++;
        fprintf(stderr, "SIMD-path render nonblack=%ld\n", nb);
        if (nb == 0) { fprintf(stderr, "FAIL: SIMD render empty\n"); return 4; }
    }
    eng.setSimd(false);

    // Exercise the random-scene + PNG-save paths.
    RandomParams rp;
    rp.minXforms = 3;
    rp.maxXforms = 5;
    rp.fastVarsOnly = true;
    rp.size = 256;
    eng.setTargetQuality(50);
    eng.randomize(rp);
    fprintf(stderr, "randomized; waiting for preview...\n");
    lastver = ver;
    for (int i = 0; i < 400; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (eng.latestImage(rgba, w, h, ver) && ver != lastver) {
            lastver = ver;
            if (!eng.isRendering() && eng.currentQuality() >= eng.targetQuality())
                break;
        }
    }
    if (eng.savePNG("engine_random.png"))
        fprintf(stderr, "wrote engine_random.png (%dx%d)\n", w, h);
    else
        fprintf(stderr, "WARN: savePNG failed\n");

    // .flam3 export + round-trip: a fresh engine must parse it and render.
    if (!eng.saveFlam3("engine_random.flam3")) {
        fprintf(stderr, "FAIL: saveFlam3 failed\n");
        return 3;
    }
    fprintf(stderr, "wrote engine_random.flam3\n");
    {
        RenderEngine reload;
        reload.setTargetQuality(20);
        if (!reload.loadFromFile("engine_random.flam3")) {
            fprintf(stderr, "FAIL: could not reload exported .flam3\n");
            return 3;
        }
        std::vector<unsigned char> r2;
        int w2 = 0, h2 = 0; uint64_t v2 = 0, lv2 = 0;
        for (int i = 0; i < 400; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if (reload.latestImage(r2, w2, h2, v2) && v2 != lv2) {
                lv2 = v2;
                if (!reload.isRendering() &&
                    reload.currentQuality() >= reload.targetQuality())
                    break;
            }
        }
        long nb = 0;
        for (size_t i = 0; i + 3 < r2.size(); i += 4)
            if (r2[i] + r2[i + 1] + r2[i + 2] > 0) nb++;
        fprintf(stderr, "round-trip render %dx%d nonblack=%ld\n", w2, h2, nb);
        if (w2 <= 0 || nb == 0) {
            fprintf(stderr, "FAIL: reloaded .flam3 did not render\n");
            return 3;
        }
    }

    fprintf(stderr, "OK\n");
    return 0;
}
