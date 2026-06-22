#include "render_engine.h"

#include <chrono>
#include <cstdio>
#include <cstring>

RenderEngine::RenderEngine() {
    std::memset(&master_, 0, sizeof(master_));
    nthreads_.store(flam3_count_nthreads());
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
