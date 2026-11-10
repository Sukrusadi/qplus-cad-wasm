// Q-Plus Viewer: telefon tarafının tek giriş noktası. Önbellek + kaba/ince geçiş + ölçüler.
// Android (JNI, ":cad" process) ve iOS (Swift köprüsü) bunu çağırır.
#pragma once

#include <functional>
#include <string>

#include "cad_import.h"

namespace qpcad {

enum class Quality { Fast = 0, Normal = 1, Fine = 2 };

struct OpenRequest {
    std::string inputPath;
    std::string ext;        // step, stp, iges, igs, brep, brp
    std::string cacheDir;   // uygulamanın önbellek klasörü
    Quality quality = Quality::Normal;
    // Dosya bu kadar baytı aşarsa önce kaba görüntü gösterilir (kaba geçiş). 0: hiçbir zaman.
    // Dosya boyutundan tahmin edilir; ilk açılışta üçgen sayısı bilinmez.
    long long coarseAboveBytes = 0;
    long long maxTriangles = 0;  // aşılırsa görüntü sadeleştirilir; 0: sınır yok
    long long rejectAboveTriangles = 0;     // sadeleşse de bunu aşan dosya reddedilir; 0: sınır yok
    unsigned long long maxStlTriangles = 0; // 0: varsayılan
    bool convertSignals = false;            // ayrı süreci olmayan iOS: bkz. CadOptions
    unsigned long long cacheMaxBytes = 300ull << 20;  // önbellek sınırı  // Mac ölçümünde kazanç ~0,1 sn, kayıp ~0,4 sn: kapalı (docs/04)
};

struct OpenEvents {
    std::function<bool(float)> progress;                              // false: iptal
    std::function<void(const fsvcad::MeshReady&)> meshReady;          // kaba ve/veya ince FSM hazır
    std::function<void(const std::string& metricsJson)> metricsReady; // ölçüler hazır
};

struct OpenResult {
    bool fromCache = false;
    std::string fsmPath;
    std::string metricsJson;
    fsvcad::CadResult cad;  // önbellekten gelince yalnızca parts/triangles/metrics dolu
};

// Hata halinde std::runtime_error fırlatır ("cancelled" dahil).
OpenResult openCad(const OpenRequest& req, const OpenEvents& ev);

// Dosya içeriğinden 64 bit anahtar (FNV-1a) + boyut + kalite + biçim sürümü.
std::string cacheKey(const std::string& path, Quality q, long long maxTriangles = 0);

// Ölçüleri JSON'a çevirir (köprüden gönderilecek ve önbellekte saklanacak biçim).
std::string metricsToJson(const fsvcad::CadResult& r);

// Önbellek `maxBytes`ı aşarsa en eski girdileri siler.
void trimCache(const std::string& dir, unsigned long long maxBytes);

}  // namespace qpcad
