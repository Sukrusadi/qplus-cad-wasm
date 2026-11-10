// Q-Plus Viewer: B-rep'ten tam ölçüler (hacim, yüzey alanı, sınır kutusu, delikler).
// Üçgen ağdan değil, OCCT'nin kesin geometrisinden hesaplanır.
#pragma once

#include <vector>

class TopoDS_Shape;

namespace qpcad {

// Deliğin bir ucu: açık ya da nasıl kapandığı.
enum class HoleEnd { Open, Flat, Cone, Round };
enum class HoleType { Through, Blind, Thread, Pocket };

struct Hole {
    double diameter = 0;        // mm, en küçük çap (kademeli delikte giriş değil, geçen çap)
    double maxDiameter = 0;     // mm, havşa/kademe varsa en büyük çap
    double depth = 0;           // mm, eksen boyunca toplam uzunluk
    double axis[3] = {0, 0, 0};
    double start[3] = {0, 0, 0};  // eksen üzerinde deliğin iki ucu (mm, montaj konumunda)
    double end[3] = {0, 0, 0};
    int steps = 1;              // farklı çap sayısı (kademeli delik > 1)
    HoleEnd endA = HoleEnd::Open, endB = HoleEnd::Open;  // start ve end uçları
    bool blind() const { return endA != HoleEnd::Open || endB != HoleEnd::Open; }
    HoleType type = HoleType::Through;
    const char* thread = nullptr;  // type Thread ise ölçü adı ("M5")
};

struct PartMetrics {
    double volume = 0;          // mm³ (katı yoksa 0)
    double surfaceArea = 0;     // mm²
    double bboxMin[3] = {0, 0, 0};
    double bboxMax[3] = {0, 0, 0};
    int solids = 0;
    bool closed = true;         // katı yok veya hacim negatif/sıfır ise false
    std::vector<Hole> holes;
    bool meshOnly = false;      // kaynak yalnız üçgen ağ (STL): delik sayılamaz, boş liste "delik yok" demek değildir
};

// `shape` konumlandırılmış (montaj örnekleri dahil) ve önceden üçgenlenmiş bileşik şekil olmalı.
// exact=true: hacim/alan B-rep'ten tam (büyük montajda saniyeler). false: üçgen ağdan, ~%1 sapma, çok hızlı.
PartMetrics computeMetrics(const TopoDS_Shape& shape, bool exact = true);

}  // namespace qpcad
