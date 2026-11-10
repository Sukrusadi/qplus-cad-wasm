// Q-Plus Viewer: STL (ikili + ASCII) okuyup FSM yazar. Kenar ve yüz türü bilgisi dosyada olmadığı için
// delik sayılmaz, yüzey ve daire yakalanmaz; hacim yalnız ağ kapalıysa anlamlıdır. Birim mm kabul edilir.
#pragma once

#include "cad_import.h"

namespace fsvcad {

CadResult importStl(const std::string& inputPath, const std::string& outputPath, const CadOptions& options,
                    const std::function<bool(float)>& progress, const std::function<void(const MeshReady&)>& onMesh);

}  // namespace fsvcad
