# qplus-cad-wasm

q-plus.io/viewer adresindeki tarayıcı görüntüleyicisinin CAD açıcısı (`qpcad.wasm`) burada bağlanır. Open CASCADE Technology (OCCT) bu dosyaya statik olarak gömülüdür. OCCT LGPL 2.1 lisanslıdır. Bu depo, OCCT'yi değiştirip `qpcad.wasm`'ı yeniden bağlayabilmeniz için gerekenleri içerir.

*English: build files for the CAD reader (`qpcad.wasm`) used by the browser viewer at q-plus.io/viewer. Open CASCADE Technology is statically linked under LGPL 2.1 with the OCCT exception. This repository lets you rebuild OCCT with your own changes and relink `qpcad.wasm`. See "Yeniden bağlama" below; the commands are the same in any language.*

## İçerik

| Yol | Ne | Lisans |
|---|---|---|
| `src/qpcad_wasm.cpp` | WebAssembly giriş noktası (`qp_open`) | MIT, `LICENSE` |
| `src/meshwise/` | STEP/IGES/BREP okuma ve FSM ağ biçimi, doesthings/Meshwise @ fa6605d'den | MIT, `LICENSE-MESHWISE` |
| `include/` | Nesne dosyalarının başlıkları | MIT, `LICENSE` |
| `obj/` | Ölçüm, açılış ve STL okuma kodu, derlenmiş nesne dosyası (wasm32) | `obj/LICENSE` |
| `CMakeLists.txt`, `build.sh`, `scripts/` | Derleme ve bağlama betikleri | MIT, `LICENSE` |
| `licenses/` | OCCT lisansı (LGPL 2.1) ve OCCT istisnası | — |
| `test/` | Node denemesi ve örnek parça (kendi ürettiğimiz bir blok) | MIT, `LICENSE` |

OCCT kaynak kodu bu depoda yok. `scripts/build-occt-wasm.sh` değiştirilmemiş V7_8_1 sürümünü https://github.com/Open-Cascade-SAS/OCCT adresinden indirir.

## Gerekenler

- Emscripten 6.0.13. Nesne dosyaları bu sürümle derlendi; başka sürüm bağlamayı bozabilir.
- CMake 3.20 ya da üstü, Ninja (isteğe bağlı), Node 20 ya da üstü (deneme için).

```sh
git clone https://github.com/emscripten-core/emsdk.git emsdk
emsdk/emsdk install 6.0.13 && emsdk/emsdk activate 6.0.13
source emsdk/emsdk_env.sh
```

## Derleme

```sh
scripts/build-occt-wasm.sh      # OCCT → third_party/occt/wasm (8 çekirdekte ~25 dk)
./build.sh                      # → out/qpcad.mjs + out/qpcad.wasm
```

## Yeniden bağlama

1. OCCT kaynağını değiştirin. Kaynak `build/occt/src-V7_8_1` altına iner; başka bir klasör için `OCCT_SRC=… scripts/build-occt-wasm.sh` kullanın.
2. `scripts/build-occt-wasm.sh` ile OCCT'yi yeniden derleyin.
3. `./build.sh` ile bağlayın. Başka bir OCCT kurulumu için `QP_OCCT_ROOT=/yol ./build.sh` kullanın.
4. Deneyin:

   ```sh
   QP_WASM_NODE=1 ./build.sh
   node test/test-node.mjs test/ornek-parca.step
   ```

   Beklenen: `hacim=256018.894 mm³`, `delik=6`, `hepsi tamam`.
5. Görüntüleyicide kullanmak için `out/qpcad.mjs` ve `out/qpcad.wasm`'ı sayfanın `/viewer/cad/` klasöründeki dosyaların yerine koyun. Kendi kopyanızı yerelde sunabilir ya da tarayıcının yerel geçersiz kılma (local overrides) özelliğini kullanabilirsiniz.

OCCT'nin ABI'sini (sınıf düzeni, sanal tablolar) değiştirmeyen her değişiklik bu nesne dosyalarıyla bağlanır. Başlık dosyalarındaki sınıf düzenini değiştiren değişikliklerde nesne dosyaları uyumsuz kalabilir. Bu durumda destek@q-plus.io adresine yazın.

## JavaScript arayüzü

```js
const M = await createQpCad({ onProgress, onMesh, onMetrics, onError, locateFile });
M.FS.writeFile('/in/parca.step', bytes);
M.ccall('qp_open', 'number', ['string', 'string', 'number'], ['/in/parca.step', 'step', 600000]);
// 0 tamam, 1 hata, 2 iptal (onProgress false dönerse)
```

## İletişim

destek@q-plus.io
