// RenderEngine - threaded, progressive wrapper around libflam3 for interactive
// preview. The GUI edits the master genome and calls requestRerender(); a
// background worker renders at increasing quality into an RGBA8 buffer,
// aborting promptly (via the flam3 progress callback) whenever a newer edit
// arrives. This class has no GUI dependency so it can be tested headless.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// flam3.h pulls in <libxml/parser.h>, which on some systems drags in ICU
// headers that include C++ <memory>. Include it here (at C++ linkage) first so
// its include guards neutralize the re-include inside the extern "C" block.
#include <libxml/parser.h>

extern "C" {
#include "flam3.h"
}

class RenderEngine {
public:
    RenderEngine();
    ~RenderEngine();

    // Load the first flame from an XML string / file. Thread-safe vs the worker.
    bool loadFromString(const std::string &xml);
    bool loadFromFile(const std::string &path);

    // The master genome, edited by the GUI thread. Returns null if none loaded.
    // After editing fields in place, call requestRerender(). For structural
    // changes use the locked helpers below.
    flam3_genome *genome() { return has_genome_ ? &master_ : nullptr; }

    // Bump the revision so the worker restarts from a fast low-quality preview.
    void requestRerender() { revision_.fetch_add(1); }

    // Copy the most recent rendered image (RGBA8). False if nothing yet.
    bool latestImage(std::vector<unsigned char> &rgba, int &w, int &h,
                     uint64_t &imageVersion);

    // Status (lock-free).
    double currentQuality() const { return cur_quality_.load(); }
    bool   isRendering()    const { return rendering_.load(); }
    int    lastRenderMs()   const { return render_ms_.load(); }
    int    threads()        const { return nthreads_.load(); }

    void   setTargetQuality(double q) { target_quality_.store(q); requestRerender(); }
    double targetQuality()  const { return target_quality_.load(); }

private:
    struct CbCtx { RenderEngine *engine; uint64_t startRev; };
    static int progressCb(void *param, double frac, int stage, double eta);
    void workerLoop();
    void clearGenome();

    flam3_genome master_;
    bool has_genome_ = false;

    std::thread worker_;
    std::mutex genome_mtx_;   // guards copy-out / load of master_
    std::mutex image_mtx_;    // guards the display buffer

    std::vector<unsigned char> display_;  // RGBA8, disp_w_*disp_h_*4
    int disp_w_ = 0, disp_h_ = 0;
    uint64_t image_version_ = 0;

    std::atomic<uint64_t> revision_{0};       // bumped on every edit
    std::atomic<uint64_t> rendered_rev_{0};   // last revision fully rendered
    std::atomic<bool>     running_{true};
    std::atomic<bool>     rendering_{false};
    std::atomic<double>   cur_quality_{0.0};
    std::atomic<double>   target_quality_{500.0};
    std::atomic<int>      render_ms_{0};
    std::atomic<int>      nthreads_{1};
};
