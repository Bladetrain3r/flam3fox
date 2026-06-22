#include "render_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// From img.h / png.c (part of flam3core). Declared here to avoid pulling img.h
// (which re-includes flam3.h) into this translation unit.
extern "C" {
typedef struct {
    char *genome;
    char *badvals;
    char *numiters;
    char *rtime;
} flam3_img_comments;
void write_png(FILE *file, void *image, int width, int height,
               flam3_img_comments *fpc, int bpc);
}

RenderEngine::RenderEngine() {
    std::memset(&master_, 0, sizeof(master_));
    nthreads_.store(flam3_count_nthreads());
    flam3_srandom();  // seed libc RNG used by flam3_random()
    worker_ = std::thread(&RenderEngine::workerLoop, this);
}

RenderEngine::~RenderEngine() {
    running_.store(false);
    revision_.fetch_add(1);  // wake / abort the worker
    if (worker_.joinable())
        worker_.join();
    clearGenome();
}

void RenderEngine::clearGenome() {
    if (has_genome_) {
        clear_cp(&master_, 0);
        std::memset(&master_, 0, sizeof(master_));
        has_genome_ = false;
    }
}

bool RenderEngine::loadFromString(const std::string &xml) {
    int ncps = 0;
    // flam3_parse_xml2 mutates its input buffer, so hand it a writable copy.
    std::vector<char> buf(xml.begin(), xml.end());
    buf.push_back('\0');
    flam3_genome *cps = flam3_parse_xml2(buf.data(), const_cast<char *>("<ui>"),
                                         flam3_defaults_on, &ncps);
    if (!cps || ncps < 1) {
        if (cps) free(cps);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(genome_mtx_);
        clearGenome();
        flam3_copy(&master_, &cps[0]);
        master_.ntemporal_samples = 1;
        has_genome_ = true;
    }

    for (int i = 0; i < ncps; i++) {
        if (cps[i].edits)
            xmlFreeDoc(cps[i].edits);
        clear_cp(&cps[i], 0);
    }
    free(cps);

    requestRerender();
    return true;
}

bool RenderEngine::loadFromFile(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp)
        return false;
    std::string xml;
    char chunk[8192];
    size_t r;
    while ((r = fread(chunk, 1, sizeof(chunk), fp)) > 0)
        xml.append(chunk, r);
    fclose(fp);
    return loadFromString(xml);
}

bool RenderEngine::randomize(const RandomParams &p) {
    // The AVX2-supported variation set (mirror of simd_var_supported).
    static int kFastVars[] = {
        VAR_LINEAR, VAR_SPHERICAL, VAR_HORSESHOE, VAR_HYPERBOLIC,
        VAR_BENT, VAR_FISHEYE, VAR_EYEFISH, VAR_BUBBLE,
        VAR_SINUSOIDAL, VAR_CYLINDER, VAR_SWIRL, VAR_DIAMOND};
    static int kAnyVar[] = {flam3_variation_random};

    int lo = std::max(1, std::min(p.minXforms, p.maxXforms));
    int hi = std::max(lo, p.maxXforms);
    int nx = lo + (int)(flam3_random01() * (hi - lo + 1));
    if (nx > hi) nx = hi;

    int *ivars = p.fastVarsOnly ? kFastVars : kAnyVar;
    int nivars = p.fastVarsOnly ? (int)(sizeof(kFastVars) / sizeof(kFastVars[0])) : 1;
    int size = std::max(16, p.size);

    {
        std::lock_guard<std::mutex> lk(genome_mtx_);
        clearGenome();
        flam3_random(&master_, ivars, nivars, p.symmetry, nx);
        master_.width = size;
        master_.height = size;
        master_.ntemporal_samples = 1;
        master_.estimator = 0.0;  // bits=33 disables DE anyway; avoid warnings
        master_.zoom = 0.0;
        master_.rotate = 0.0;
        has_genome_ = true;

        // Auto-frame: estimate the attractor's extent and fit it to the canvas.
        flam3_frame fr;
        std::memset(&fr, 0, sizeof(fr));
        flam3_init_frame(&fr);
        double bmin[2] = {-1, -1}, bmax[2] = {1, 1};
        flam3_estimate_bounding_box(&master_, 0.01, 100000, bmin, bmax, &fr.rc);
        double ex = bmax[0] - bmin[0], ey = bmax[1] - bmin[1];
        if (ex < 1e-6) ex = 1e-6;
        if (ey < 1e-6) ey = 1e-6;
        master_.center[0] = 0.5 * (bmin[0] + bmax[0]);
        master_.center[1] = 0.5 * (bmin[1] + bmax[1]);
        double fit = std::min((double)size / ex, (double)size / ey) * 0.9;
        double lim = p.zoomFitMax > p.zoomFitMin ? p.zoomFitMax : p.zoomFitMin;
        double mul = p.zoomFitMin + flam3_random01() * (lim - p.zoomFitMin);
        master_.pixels_per_unit = fit * mul;
    }

    requestRerender();
    return true;
}

bool RenderEngine::savePNG(const std::string &path) {
    std::vector<unsigned char> rgba;
    int w, h;
    {
        std::lock_guard<std::mutex> lk(image_mtx_);
        if (disp_w_ <= 0 || disp_h_ <= 0 || display_.empty())
            return false;
        rgba = display_;
        w = disp_w_;
        h = disp_h_;
    }
    FILE *fp = fopen(path.c_str(), "wb");
    if (!fp)
        return false;
    char empty[] = "";
    flam3_img_comments comm{empty, empty, empty, empty};
    write_png(fp, rgba.data(), w, h, &comm, 1);  // RGBA8, 1 byte/channel
    fclose(fp);
    return true;
}

bool RenderEngine::latestImage(std::vector<unsigned char> &rgba, int &w, int &h,
                               uint64_t &imageVersion) {
    std::lock_guard<std::mutex> lk(image_mtx_);
    if (disp_w_ <= 0 || disp_h_ <= 0 || display_.empty())
        return false;
    rgba = display_;
    w = disp_w_;
    h = disp_h_;
    imageVersion = image_version_;
    return true;
}

int RenderEngine::progressCb(void *param, double, int, double) {
    CbCtx *c = static_cast<CbCtx *>(param);
    // Abort the in-flight render the moment a newer edit (or shutdown) lands.
    return (c->engine->revision_.load() != c->startRev) ? 1 : 0;
}

void RenderEngine::workerLoop() {
    using clock = std::chrono::steady_clock;

    while (running_.load()) {
        uint64_t rev = revision_.load();
        if (!has_genome_ || rev == rendered_rev_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        // Take a private copy of the genome to render.
        flam3_genome local;
        std::memset(&local, 0, sizeof(local));
        {
            std::lock_guard<std::mutex> lk(genome_mtx_);
            if (!has_genome_) continue;
            flam3_copy(&local, &master_);
        }
        local.ntemporal_samples = 1;

        int w = local.width, h = local.height;
        if (w <= 0 || h <= 0) {
            rendered_rev_.store(rev);
            clear_cp(&local, 0);
            continue;
        }

        const double target = target_quality_.load();
        std::vector<unsigned char> buf((size_t)w * h * 4);
        bool aborted = false;
        rendering_.store(true);

        // Progressive: render at geometrically increasing quality, publishing
        // each completed level, until we reach the target.
        for (double q = 1.0;; q *= 8.0) {
            double useq = (q < target) ? q : target;

            local.sample_density = useq;

            CbCtx ctx{this, rev};
            flam3_frame f;
            std::memset(&f, 0, sizeof(f));
            flam3_init_frame(&f);
            f.genomes = &local;
            f.ngenomes = 1;
            f.bits = 33;
            f.time = 0.0;
            f.pixel_aspect_ratio = 1.0;
            f.nthreads = nthreads_.load();
            f.sub_batch_size = 10000;
            f.bytes_per_channel = 1;
            f.progress = &RenderEngine::progressCb;
            f.progress_parameter = &ctx;

            stat_struct stats;
            std::memset(&stats, 0, sizeof(stats));

            auto t0 = clock::now();
            int rv = flam3_render(&f, buf.data(), flam3_field_both, 4, 0, &stats);
            auto t1 = clock::now();

            if (revision_.load() != rev) { aborted = true; break; }

            if (rv == 0) {
                std::lock_guard<std::mutex> lk(image_mtx_);
                display_ = buf;
                disp_w_ = w;
                disp_h_ = h;
                ++image_version_;
                cur_quality_.store(useq);
                render_ms_.store((int)std::chrono::duration_cast<
                    std::chrono::milliseconds>(t1 - t0).count());
            }

            if (useq >= target)
                break;
        }

        rendering_.store(false);
        if (!aborted)
            rendered_rev_.store(rev);
        clear_cp(&local, 0);
    }
}
