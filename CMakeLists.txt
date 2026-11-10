# qpcad.mjs + qpcad.wasm: OCCT (statik) + sarmalayıcı + Meshwise kaynakları + obj/ altındaki nesne dosyaları.
# Derleme: ./build.sh. OCCT'yi değiştirip yeniden bağlamak için: README.md § Yeniden bağlama.
cmake_minimum_required(VERSION 3.20)
project(qpcad_wasm CXX)
set(CMAKE_CXX_STANDARD 17)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "emcmake cmake ile çalıştırın (README.md)")
endif()
set(QP_OCCT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/occt/wasm" CACHE PATH "OCCT kurulum klasörü (lib/, include/)")
option(QP_WASM_NODE "Node ortamını da ekle (test/test-node.mjs için)" OFF)

set(TK TKDESTEP TKDEIGES TKDE TKXCAF TKBinXCAF TKBin TKBinL TKVCAF TKCAF TKLCAF TKCDF TKV3d TKService TKHLR
  TKXSBase TKMesh TKShHealing TKBool TKBO TKPrim TKTopAlgo TKGeomAlgo TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel)
set(LIBS)
foreach(t ${TK})
  if(EXISTS "${QP_OCCT_ROOT}/lib/lib${t}.a")
    list(APPEND LIBS "${QP_OCCT_ROOT}/lib/lib${t}.a")
  endif()
endforeach()
if(NOT LIBS)
  message(FATAL_ERROR "OCCT bulunamadı: ${QP_OCCT_ROOT}/lib (scripts/build-occt-wasm.sh)")
endif()

set(OBJ ${CMAKE_CURRENT_SOURCE_DIR}/obj)
add_executable(qpcad src/qpcad_wasm.cpp src/meshwise/cad_import.cpp
  ${OBJ}/part_metrics.o ${OBJ}/qp_open.o ${OBJ}/stl_import.o)
set_source_files_properties(${OBJ}/part_metrics.o ${OBJ}/qp_open.o ${OBJ}/stl_import.o PROPERTIES EXTERNAL_OBJECT TRUE)
set_target_properties(qpcad PROPERTIES SUFFIX ".mjs")
target_include_directories(qpcad PRIVATE include src/meshwise ${QP_OCCT_ROOT}/include/opencascade)
# OCCT ve nesne dosyaları bu tanımla derlendi; Standard_ErrorHandler düzeni aynı olmalı.
target_compile_definitions(qpcad PRIVATE OCC_CONVERT_SIGNALS)
target_compile_options(qpcad PRIVATE -O2 -fexceptions -Wno-deprecated-declarations)

set(ENV_LIST "web,worker")
if(QP_WASM_NODE)
  set(ENV_LIST "web,worker,node")
endif()
target_link_options(qpcad PRIVATE
  -O2 -fexceptions
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createQpCad
  -sENVIRONMENT=${ENV_LIST}
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB
  -sSTACK_SIZE=8MB
  -sFORCE_FILESYSTEM=1
  "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','UTF8ToString']"
  "-sEXPORTED_FUNCTIONS=['_qp_open']")
target_link_libraries(qpcad PRIVATE ${LIBS} ${LIBS})
