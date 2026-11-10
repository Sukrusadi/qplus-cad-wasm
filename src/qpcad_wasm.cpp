// Tarayıcı (Web Worker) giriş noktası: qpcad::openCad'i wasm'a açar. Bkz. README.md
// Dosyalar MEMFS'te: JS girdiyi /in/<ad> olarak yazar, FSM ve ölçüler /cache'e düşer.
// Olaylar Module üzerindeki geri çağrılara gider; hata kodları Android/iOS köprüsüyle aynı.
#include <emscripten.h>

#include <OSD_ThreadPool.hxx>

#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>

#include "qp_open.h"

namespace fs = std::filesystem;

EM_JS_DEPS(qpcad, "$UTF8ToString");

EM_JS(int, js_progress, (double v), {
    if (typeof Module.onProgress !== 'function') return 1;
    return Module.onProgress(v) === false ? 0 : 1;
});

EM_JS(void, js_mesh, (const char* path, int parts, double tris, int coarse, int simplified, double fullTris), {
    if (typeof Module.onMesh !== 'function') return;
    Module.onMesh({ fsmPath: UTF8ToString(path), parts: parts, triangles: tris, coarse: !!coarse,
                    simplified: !!simplified, fullTriangles: fullTris });
});

EM_JS(void, js_metrics, (const char* json), {
    if (typeof Module.onMetrics === 'function') Module.onMetrics(UTF8ToString(json));
});

EM_JS(void, js_error, (const char* code, const char* message), {
    if (typeof Module.onError === 'function') Module.onError(UTF8ToString(code), UTF8ToString(message));
});

namespace {

const char* kCache = "/cache";

// JS girdiyi yazmadan önce klasörler hazır olsun (modül yüklenirken çalışır).
struct Dirs {
    Dirs() {
        std::error_code ec;
        fs::create_directories("/in", ec);
        fs::create_directories(kCache, ec);
    }
} dirs;

// Önbellek yok: önceki açılışın FSM/JSON dosyaları silinir (JS onları olay anında okumuş olmalı).
void clearCache() {
    std::error_code ec;
    for (auto& d : fs::directory_iterator(kCache, ec)) fs::remove_all(d.path(), ec);
}

// QpCadPlugin.java / QpCadPlugin.swift ile aynı eşleme.
void fail(const std::string& kernelError) {
    static const char* broken =
        "Dosyada okunabilir bir parça yok ya da dosya yarım inmiş. Gönderen kişiden STEP AP214 olarak yeniden istemeni öneririz.";
    std::string m = kernelError;
    for (char& c : m)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    auto has = [&](const char* s) { return m.find(s) != std::string::npos; };
    if (has("could not be read")) js_error("okunamadi", broken);
    else if (has("no usable geometry") || has("no shapes") || has("no geometry") || has("triangulated"))
        js_error("geometri-yok", broken);
    else if (has("too large") || has("bad_alloc"))
        js_error("cokme", "Dosya açılırken durdu. Bu cihaz için çok büyük olabilir.");
    else if (has("unsupported"))
        js_error("bicim", "Bu dosya biçimi henüz desteklenmiyor. Gönderen kişiden STEP olarak iste.");
    else js_error("hata", "Dosya işlenirken bir sorun çıktı. Başka bir dosyayla dene.");
}

}  // namespace

// 0: tamam, 1: hata (onError çağrıldı), 2: iptal (onProgress false döndü; onError("cancelled")).
extern "C" EMSCRIPTEN_KEEPALIVE int qp_open(const char* path, const char* ext, double maxTriangles) {
    static bool pool = (OSD_ThreadPool::DefaultPool(1), true);  // thread yok: yalnız çağıran thread
    (void)pool;
    clearCache();
    qpcad::OpenRequest req;
    req.inputPath = path ? path : "";
    req.ext = ext ? ext : "";
    req.cacheDir = kCache;
    req.quality = qpcad::Quality::Normal;
    req.coarseAboveBytes = 0;  // kaba geçiş kapalı
    req.maxTriangles = maxTriangles > 0 ? (long long)maxTriangles : 0;
    req.cacheMaxBytes = ~0ull;  // silmeyi clearCache yapar
    qpcad::OpenEvents ev;
    ev.progress = [](float v) -> bool { return js_progress(v) != 0; };
    ev.meshReady = [](const fsvcad::MeshReady& m) {
        js_mesh(m.path.c_str(), m.parts, (double)m.triangles, m.coarse, m.simplified, (double)m.fullTriangles);
    };
    ev.metricsReady = [](const std::string& json) { js_metrics(json.c_str()); };
    try {
        qpcad::openCad(req, ev);
        return 0;
    } catch (const std::bad_alloc&) {
        fail("bad_alloc");
    } catch (const std::exception& e) {
        if (std::string(e.what()) == "cancelled") {
            js_error("cancelled", "İptal edildi");
            return 2;
        }
        fail(e.what());
    } catch (...) {
        fail("The CAD kernel failed to import this file");
    }
    return 1;
}
