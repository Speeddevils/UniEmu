#include "texcache.h"
#include <algorithm>
#include "stb_image.h"
#include "util.h"

static const int kMaxH = 512;      // capas são reduzidas para no máximo 512 px de altura
static const size_t kMaxTex = 600; // acima disso, libera as menos usadas

TexCache::TexCache() { th_ = std::thread(&TexCache::worker, this); }

TexCache::~TexCache() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    for (auto& [p, t] : cache_) if (t.tex) glDeleteTextures(1, &t.tex);
}

const CoverTex* TexCache::get(const std::string& path) {
    auto it = cache_.find(path);
    if (it == cache_.end()) {
        cache_[path] = CoverTex();
        {
            std::lock_guard<std::mutex> lk(m_);
            todo_.push_front(path);   // os mais recentes (visíveis agora) primeiro
        }
        cv_.notify_one();
        return nullptr;
    }
    it->second.lastUse = SDL_GetTicks();
    return it->second.tex ? &it->second : nullptr;
}

void TexCache::invalidate(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        if (it->second.tex) glDeleteTextures(1, &it->second.tex);
        cache_.erase(it);
    }
}

void TexCache::pump() {
    for (int n = 0; n < 6; n++) {
        Decoded d;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (ready_.empty()) break;
            d = std::move(ready_.front());
            ready_.pop_front();
        }
        auto it = cache_.find(d.path);
        if (it == cache_.end()) continue;
        if (!d.ok) { it->second.failed = true; continue; }
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.w, d.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        it->second.tex = t;
        it->second.w = d.w;
        it->second.h = d.h;
        it->second.lastUse = SDL_GetTicks();
    }
    if (cache_.size() > kMaxTex) {   // libera as menos usadas
        std::vector<std::pair<Uint32, std::string>> v;
        for (auto& [p, t] : cache_) if (t.tex) v.push_back({t.lastUse, p});
        std::sort(v.begin(), v.end());
        for (size_t i = 0; i < v.size() && cache_.size() > kMaxTex * 3 / 4; i++) {
            auto it = cache_.find(v[i].second);
            glDeleteTextures(1, &it->second.tex);
            cache_.erase(it);
        }
    }
}

void TexCache::worker() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return stop_ || !todo_.empty(); });
            if (stop_) return;
            path = todo_.front();
            todo_.pop_front();
        }
        Decoded d;
        d.path = path;
        std::vector<uint8_t> file;
        if (util::readFile(path, file)) {
            int w, h, c;
            unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &c, 4);
            if (px) {
                // reduz pela metade (média 2x2) até caber
                std::vector<uint8_t> img(px, px + (size_t)w * h * 4);
                stbi_image_free(px);
                while (h > kMaxH && w > 2) {
                    int nw = w / 2, nh = h / 2;
                    std::vector<uint8_t> o((size_t)nw * nh * 4);
                    for (int y = 0; y < nh; y++)
                        for (int x = 0; x < nw; x++)
                            for (int k = 0; k < 4; k++) {
                                int s = img[((2 * y) * w + 2 * x) * 4 + k] + img[((2 * y) * w + 2 * x + 1) * 4 + k] +
                                        img[((2 * y + 1) * w + 2 * x) * 4 + k] + img[((2 * y + 1) * w + 2 * x + 1) * 4 + k];
                                o[((size_t)y * nw + x) * 4 + k] = (uint8_t)(s / 4);
                            }
                    img.swap(o);
                    w = nw;
                    h = nh;
                }
                d.rgba.swap(img);
                d.w = w;
                d.h = h;
                d.ok = true;
            }
        }
        std::lock_guard<std::mutex> lk(m_);
        ready_.push_back(std::move(d));
    }
}
