// Kaynak: doesthings/Meshwise @ fa6605d, MIT. Telif (c) 2026 doesthings. Bkz. LICENSE-MESHWISE
// STEP / IGES / BREP import through Open CASCADE. Produces an FSM mesh file.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "part_metrics.h"

namespace fsvcad {

struct CadOptions {
    // Linear deflection as a fraction of the model's bounding-box diagonal.
    double relativeDeflection = 0.0008;
    // Angular deflection in radians.
    double angularDeflection = 0.35;
    // Q-Plus: >0 ise önce bu kaba değerle üçgenlenip `coarseOutputPath`e yazılır (model hemen
    // görünsün), sonra normal kaliteye geçilir.
    double coarseRelativeDeflection = 0;
    double coarseAngularDeflection = 0.5;
    std::string coarseOutputPath;
    // Q-Plus: false ise tam ölçüler (hacim, alan, delik) hesaplanmaz.
    bool computeMetrics = true;
    // Q-Plus: >0 ise üçgen sayısı bunu aşınca görüntü sadeleştirilir ("telefon için büyük").
    // Hacim, alan ve delikler B-rep'ten geldiği için etkilenmez.
    long long maxTriangles = 0;
    // Q-Plus: >0 ise sadeleştirmenin son kademesinden sonra hâlâ bunu aşan dosya reddedilir.
    long long rejectAboveTriangles = 0;
    // Q-Plus: >0 ise STL okuyucunun üçgen sınırı (0: stl_import.cpp'deki varsayılan).
    unsigned long long maxStlTriangles = 0;
    // Q-Plus: açılış süresince OCCT sinyal işleyicileri kurulur; bellek hatası istisnaya çevrilir.
    // Tüm sürece etki eder. Ayrı süreci olmayan iOS içindir.
    bool convertSignals = false;
};

// Q-Plus: bir FSM dosyası yazıldığında çağrılır (kaba geçişte bir kez daha).
struct MeshReady {
    std::string path;
    int parts = 0;
    long long triangles = 0;
    bool coarse = false;
    double msSinceStart = 0;
    bool simplified = false;
    long long fullTriangles = 0;  // sadeleştirmeden önceki üçgen sayısı
};

struct CadResult {
    int parts = 0;
    long long triangles = 0;
    std::vector<std::string> warnings;
    // Q-Plus: B-rep'ten tam ölçüler ve aşama süreleri (ms).
    qpcad::PartMetrics metrics;
    double msRead = 0, msTransfer = 0, msMesh = 0, msMetrics = 0, msWrite = 0;
    bool simplified = false;
    long long fullTriangles = 0;
    double msCoarse = 0;        // kaba üçgenleme + yazma (0: kaba geçiş yok)
    double msFirstVisible = 0;  // başlangıçtan ilk FSM dosyasının yazılmasına kadar
};

// Reads `inputPath` (format from `ext`: step/stp/iges/igs/brep/brp) and writes an FSM file
// to `outputPath`. `progress` receives values in [0,1] and returns false to cancel.
// Throws std::runtime_error with a user-readable message on failure.
// Q-Plus: `onMesh` model çizilebilir olduğu anda çağrılır; ölçüler daha sonra hesaplanır.
CadResult importCad(const std::string& inputPath, const std::string& ext, const std::string& outputPath,
                    const CadOptions& options, const std::function<bool(float)>& progress,
                    const std::function<void(const MeshReady&)>& onMesh = nullptr);

}  // namespace fsvcad
