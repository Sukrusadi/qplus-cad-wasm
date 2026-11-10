// Kaynak: doesthings/Meshwise @ fa6605d, MIT. Telif (c) 2026 doesthings. Bkz. LICENSE-MESHWISE
#include "cad_import.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Interface_Static.hxx>
#include <Message_ProgressIndicator.hxx>
#include <OSD.hxx>
#include <Message_ProgressScope.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <StepData_ConfParameters.hxx>
#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <XCAFPrs_IndexedDataMapOfShapeStyle.hxx>
#include <XCAFPrs_Style.hxx>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "fsm_format.h"
#include "stl_import.h"

namespace fsvcad {

namespace {

// Bridges OCCT progress reporting to our callback and supports cancellation.
// Parallel meshing advances the indicator from OCCT pool threads, and OCCT calls Show()
// on whichever thread advanced it. The callback goes through JNI, which is only valid on
// the thread that called into native code, so reports from other threads are dropped
// (the calling thread takes part in the work, so progress still gets through).
class ProgressBridge : public Message_ProgressIndicator {
public:
    ProgressBridge(const std::function<bool(float)>& fn, float lo, float hi)
        : fn_(fn), lo_(lo), hi_(hi), owner_(std::this_thread::get_id()) {}
    Standard_Boolean UserBreak() override { return cancelled_.load(std::memory_order_relaxed); }
    void Show(const Message_ProgressScope&, const Standard_Boolean) override {
        if (!fn_ || std::this_thread::get_id() != owner_) return;
        float v = lo_ + (hi_ - lo_) * (float)GetPosition();
        if (v - last_ < 0.004f) return;
        last_ = v;
        if (!fn_(v)) cancelled_ = true;
    }
    bool cancelled() const { return cancelled_; }

private:
    const std::function<bool(float)>& fn_;
    float lo_, hi_;
    float last_ = -1.f;
    std::thread::id owner_;
    std::atomic<bool> cancelled_{false};
};

// Q-Plus: Türkçe CAD programları bazen adları UTF-8 yerine Windows-1254 ile yazıyor (ham 0xF6 = ö).
// Dosyada geçersiz UTF-8 baytı varsa OCCT'ye CP1254 olarak okumasını söyleriz.
bool looksLikeUtf8(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return true;
    std::vector<unsigned char> buf(1 << 16);
    int need = 0;
    bool ok = true;
    size_t n;
    while (ok && (n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            unsigned char b = buf[i];
            if (need) {
                if ((b & 0xC0) != 0x80) { ok = false; break; }
                --need;
            } else if (b >= 0x80) {
                if ((b & 0xE0) == 0xC0) need = 1;
                else if ((b & 0xF0) == 0xE0) need = 2;
                else if ((b & 0xF8) == 0xF0) need = 3;
                else { ok = false; break; }
            }
        }
    }
    std::fclose(f);
    return ok;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

std::string labelName(const TDF_Label& label) {
    Handle(TDataStd_Name) name;
    if (!label.IsNull() && label.FindAttribute(TDataStd_Name::GetID(), name)) {
        // Q-Plus: her zaman UTF-8. OCCT'nin IsAscii() 0xFF'e kadar "ASCII" sayıyor; ö, ş, ğ ham bayt
        // olarak kalıyordu.
        const TCollection_ExtendedString& ext = name->Get();
        int len = ext.LengthOfCString();
        std::string out((size_t)len + 1, '\0');
        Standard_PCharacter p = out.data();
        ext.ToUTF8CString(p);
        out.resize(std::strlen(out.c_str()));
        return out;
    }
    return std::string();
}

uint32_t toRgba(const Quantity_ColorRGBA& c) {
    // Quantity colours are linear RGB; convert to sRGB for display.
    NCollection_Vec3<float> srgb = Quantity_Color::Convert_LinearRGB_To_sRGB(c.GetRGB().Rgb());
    auto b = [](float v) { return (uint32_t)std::lround(std::max(0.f, std::min(1.f, v)) * 255.f); };
    return b(srgb.r()) | (b(srgb.g()) << 8) | (b(srgb.b()) << 16) | (b(c.Alpha()) << 24);
}

struct PartMesh {
    std::string name;
    uint32_t color = 0;
    bool hasColor = false;
    std::vector<float> positions, normals;
    std::vector<uint32_t> colors;  // per vertex (only kept when faces differ)
    std::vector<uint32_t> indices, edges;
    bool mixedColors = false;
    // Q-Plus: ölçüm yakalaması için kesin yüz/kenar bilgisi (fsm_format.h).
    std::vector<uint32_t> triFace, edgeId;
    std::vector<float> faces, edgeInfo;
};

// Q-Plus: yüzün türü ve parametreleri, parçanın montajdaki konumunda.
void addFaceRecord(const TopoDS_Face& face, const gp_Trsf& nodeTrsf, std::vector<float>& out) {
    float r[fsv::kFsmFaceRecord] = {5};
    BRepAdaptor_Surface s(face, false);
    double u0, u1, v0, v1;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    BRepGProp_Face gf(face);
    gp_Pnt P;
    gp_Vec N;
    gf.Normal(0.5 * (u0 + u1), 0.5 * (v0 + v1), P, N);
    auto put = [&](int at, const gp_XYZ& v) {
        r[at] = (float)v.X();
        r[at + 1] = (float)v.Y();
        r[at + 2] = (float)v.Z();
    };
    switch (s.GetType()) {
        case GeomAbs_Plane: {
            r[0] = 0;
            put(1, s.Plane().Location().Transformed(nodeTrsf).XYZ());
            if (N.SquareMagnitude() > 1e-20) put(4, gp_Dir(N).Transformed(nodeTrsf).XYZ());
            break;
        }
        case GeomAbs_Cylinder: {
            r[0] = 1;
            const gp_Cylinder c = s.Cylinder();
            const gp_Ax1 ax = c.Axis();
            put(1, ax.Location().Transformed(nodeTrsf).XYZ());
            put(4, ax.Direction().Transformed(nodeTrsf).XYZ());
            r[7] = (float)c.Radius();
            r[8] = (float)v0;
            r[9] = (float)v1;
            gp_Vec d(ax.Direction()), ap(ax.Location(), P);
            r[10] = N.Dot(ap - d * ap.Dot(d)) < 0 ? 1.f : 0.f;  // normal eksene bakıyorsa delik
            r[11] = (float)(u1 - u0);
            break;
        }
        case GeomAbs_Cone: r[0] = 2; break;
        case GeomAbs_Sphere: r[0] = 3; break;
        case GeomAbs_Torus: r[0] = 4; break;
        default: break;
    }
    out.insert(out.end(), r, r + fsv::kFsmFaceRecord);
}

// Q-Plus: kenarın türü, uzunluğu; daireyse merkez, eksen, yarıçap. `polyLength`: üçgen ağından uzunluk.
void addEdgeRecord(const TopoDS_Edge& edge, const gp_Trsf& nodeTrsf, double polyLength, std::vector<float>& out) {
    float r[fsv::kFsmEdgeRecord] = {2, (float)polyLength};
    BRepAdaptor_Curve c(edge);
    const double f = c.FirstParameter(), l = c.LastParameter();
    if (c.GetType() == GeomAbs_Line) {
        r[0] = 0;
        r[1] = (float)c.Value(f).Distance(c.Value(l));
    } else if (c.GetType() == GeomAbs_Circle) {
        const gp_Circ ci = c.Circle();
        const gp_XYZ o = ci.Location().Transformed(nodeTrsf).XYZ(), a = ci.Axis().Direction().Transformed(nodeTrsf).XYZ();
        r[0] = 1;
        r[1] = (float)(ci.Radius() * (l - f));
        r[2] = (float)o.X(); r[3] = (float)o.Y(); r[4] = (float)o.Z();
        r[5] = (float)a.X(); r[6] = (float)a.Y(); r[7] = (float)a.Z();
        r[8] = (float)ci.Radius();
        r[9] = std::abs((l - f) - 2 * M_PI) < 1e-3 ? 1.f : 0.f;
    }
    out.insert(out.end(), r, r + fsv::kFsmEdgeRecord);
}

// Appends all faces of `shape` (transformed by `loc`) to `out`.
void appendShape(const TopoDS_Shape& shape, const TopLoc_Location& loc, uint32_t partColor,
                 const XCAFPrs_IndexedDataMapOfShapeStyle* styles, PartMesh& out) {
    const gp_Trsf nodeTrsf = loc.Transformation();
    const bool mirrored = nodeTrsf.IsNegative();
    TopTools_IndexedMapOfShape seenEdges;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location fl;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, fl);
        if (tri.IsNull() || tri->NbTriangles() == 0) continue;
        if (!tri->HasNormals()) BRepLib_ToolTriangulatedShape::ComputeNormals(face, tri);
        const gp_Trsf tr = nodeTrsf * fl.Transformation();
        const bool reversed = (face.Orientation() == TopAbs_REVERSED) != mirrored;
        const bool flipNormals = face.Orientation() == TopAbs_REVERSED;

        uint32_t faceColor = partColor;
        if (styles) {
            const XCAFPrs_Style* st = styles->Seek(face);
            if (st && st->IsSetColorSurf()) faceColor = toRgba(st->GetColorSurfRGBA());
        }
        if (faceColor != partColor) out.mixedColors = true;

        const uint32_t base = (uint32_t)(out.positions.size() / 3);
        const uint32_t faceId = (uint32_t)(out.faces.size() / fsv::kFsmFaceRecord);
        addFaceRecord(face, nodeTrsf, out.faces);
        const int nn = tri->NbNodes();
        for (int i = 1; i <= nn; ++i) {
            gp_Pnt p = tri->Node(i).Transformed(tr);
            out.positions.insert(out.positions.end(), {(float)p.X(), (float)p.Y(), (float)p.Z()});
            gp_Dir n(0, 0, 1);
            if (tri->HasNormals()) {
                gp_Vec v = tri->Normal(i);
                if (v.SquareMagnitude() > 1e-20) n = gp_Dir(v).Transformed(tr);
            }
            if (flipNormals) n.Reverse();
            out.normals.insert(out.normals.end(), {(float)n.X(), (float)n.Y(), (float)n.Z()});
            out.colors.push_back(faceColor);
        }
        const int nt = tri->NbTriangles();
        for (int i = 1; i <= nt; ++i) {
            int a, b, c;
            tri->Triangle(i).Get(a, b, c);
            if (reversed) std::swap(b, c);
            out.indices.insert(out.indices.end(), {base + (uint32_t)(a - 1), base + (uint32_t)(b - 1), base + (uint32_t)(c - 1)});
            out.triFace.push_back(faceId);
        }
        // Boundary edges of the face become CAD edge lines (each shared edge once).
        for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(ee.Current());
            if (seenEdges.Contains(edge) || BRep_Tool::Degenerated(edge)) continue;
            seenEdges.Add(edge);
            Handle(Poly_PolygonOnTriangulation) poly = BRep_Tool::PolygonOnTriangulation(edge, tri, fl);
            if (poly.IsNull()) continue;
            const uint32_t edgeNo = (uint32_t)(out.edgeInfo.size() / fsv::kFsmEdgeRecord);
            double polyLength = 0;
            const TColStd_Array1OfInteger& nodes = poly->Nodes();
            for (int k = nodes.Lower(); k < nodes.Upper(); ++k) {
                int na = nodes(k), nb = nodes(k + 1);
                if (na < 1 || nb < 1 || na > nn || nb > nn) continue;
                out.edges.push_back(base + (uint32_t)(na - 1));
                out.edges.push_back(base + (uint32_t)(nb - 1));
                out.edgeId.push_back(edgeNo);
                polyLength += tri->Node(na).Transformed(tr).Distance(tri->Node(nb).Transformed(tr));
            }
            addEdgeRecord(edge, nodeTrsf, polyLength, out.edgeInfo);
        }
    }
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("Could not write the converted mesh");
    size_t n = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (n != bytes.size()) throw std::runtime_error("Could not write the converted mesh (disk full?)");
}

// Q-Plus: belgedeki yaprak parçaları güncel üçgenlemeleriyle FSM olarak yazar (kaba ve ince geçişte ortak).
// Önce geçici dosyaya yazılır, sonra yeniden adlandırılır: okuyan taraf yarım dosya görmez.
int writeParts(const Handle(TDocStd_Document)& doc, const std::string& outputPath, long long& triangles) {
    triangles = 0;
    // Walk leaf nodes with resolved styles and locations.
    std::vector<PartMesh> parts;
    XCAFPrs_Style defStyle;
    XCAFPrs_DocumentExplorer explorer(doc, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes, defStyle);
    std::unordered_map<std::string, int> nameCount;
    for (; explorer.More(); explorer.Next()) {
        const XCAFPrs_DocumentNode& node = explorer.Current();
        TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(node.RefLabel);
        if (shape.IsNull()) continue;
        PartMesh pm;
        // Instance name, falling back to the product name.
        pm.name = labelName(node.Label);
        // Q-Plus: SolidWorks örnek adları "NAUO12" gibi anlamsız; o zaman ürün adını kullan.
        if (pm.name.empty() || pm.name == "=>[0:1:1:1]" || pm.name.rfind("NAUO", 0) == 0) pm.name = labelName(node.RefLabel);
        if (pm.name.empty()) pm.name = "Part";
        if (node.Style.IsSetColorSurf()) {
            pm.color = toRgba(node.Style.GetColorSurfRGBA());
            pm.hasColor = true;
        }
        // Per-face colours defined on the referenced shape.
        XCAFPrs_IndexedDataMapOfShapeStyle styles;
        XCAFPrs::CollectStyleSettings(node.RefLabel, TopLoc_Location(), styles);
        // Faces inherit the instance colour unless they override it.
        uint32_t base = pm.hasColor ? pm.color : 0u;
        appendShape(shape, node.Location, base, styles.IsEmpty() ? nullptr : &styles, pm);
        if (pm.indices.empty()) continue;
        if (!pm.hasColor && pm.mixedColors) {
            // No instance colour but coloured faces: keep vertex colours, faces without a
            // colour use a neutral grey.
            for (uint32_t& c : pm.colors) if (c == 0) c = 0xFFC8C8C8u;
        }
        int& count = nameCount[pm.name];
        if (++count > 1) pm.name += " (" + std::to_string(count) + ")";
        triangles += (long long)(pm.indices.size() / 3);
        parts.push_back(std::move(pm));
    }
    if (parts.empty()) throw std::runtime_error("The CAD model could not be triangulated");
    if (parts.size() > 65535) throw std::runtime_error("Too many parts (limit is 65535)");

    fsv::FsmWriter writer((uint32_t)parts.size());
    for (const PartMesh& pm : parts) {
        uint32_t flags = (pm.hasColor ? fsv::kFsmFlagFileColor : 0) | (pm.mixedColors ? fsv::kFsmFlagVertexColors : 0);
        writer.part(pm.name, pm.hasColor ? (pm.color | 0xFF000000u) : 0u, flags, pm.positions.data(), pm.normals.data(),
                    pm.mixedColors ? pm.colors.data() : nullptr, (uint32_t)(pm.positions.size() / 3), pm.indices.data(),
                    (uint32_t)pm.indices.size(), pm.edges.data(), (uint32_t)pm.edges.size(), pm.triFace, pm.edgeId, pm.faces, pm.edgeInfo);
    }
    const std::string tmp = outputPath + ".part";
    writeFile(tmp, writer.bytes());
    if (std::rename(tmp.c_str(), outputPath.c_str()) != 0) throw std::runtime_error("Could not write the converted mesh");
    return (int)parts.size();
}

CadResult importImpl(const std::string& inputPath, const std::string& extIn, const std::string& outputPath,
                     const CadOptions& options, const std::function<bool(float)>& progress,
                     const std::function<void(const MeshReady&)>& onMesh) {
    CadResult result;
    const auto clockStart = std::chrono::steady_clock::now();
    const std::string ext = lower(extIn);
    auto clock0 = std::chrono::steady_clock::now();
    auto lap = [&]() {
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - clock0).count();
        clock0 = now;
        return ms;
    };
    auto report = [&](float v) {
        if (progress && !progress(v)) throw std::runtime_error("cancelled");
    };
    report(0.01f);

    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);
    bool haveDoc = false;
    TopoDS_Shape plainShape;

    if (ext == "step" || ext == "stp") {
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(false);
        reader.SetPropsMode(false);
        reader.SetGDTMode(false);
        reader.SetMatMode(false);
        reader.SetViewMode(false);
        StepData_ConfParameters params;
        params.InitFromStatic();
        params.ReadCodePage = looksLikeUtf8(inputPath) ? Resource_FormatType_UTF8 : Resource_FormatType_CP1254;
        if (reader.ReadFile(inputPath.c_str(), params) != IFSelect_RetDone)
            throw std::runtime_error("This STEP file could not be read");
        result.msRead = lap();
        report(0.25f);
        Handle(ProgressBridge) bridge = new ProgressBridge(progress, 0.25f, 0.55f);
        if (!reader.Transfer(doc, bridge->Start())) {
            if (bridge->cancelled()) throw std::runtime_error("cancelled");
            throw std::runtime_error("The STEP file contains no usable geometry");
        }
        haveDoc = true;
    } else if (ext == "iges" || ext == "igs") {
        IGESCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(false);
        if (reader.ReadFile(inputPath.c_str()) != IFSelect_RetDone)
            throw std::runtime_error("This IGES file could not be read");
        report(0.25f);
        Handle(ProgressBridge) bridge = new ProgressBridge(progress, 0.25f, 0.55f);
        if (!reader.Transfer(doc, bridge->Start())) {
            if (bridge->cancelled()) throw std::runtime_error("cancelled");
            throw std::runtime_error("The IGES file contains no usable geometry");
        }
        haveDoc = true;
    } else if (ext == "brep" || ext == "brp") {
        BRep_Builder builder;
        Handle(ProgressBridge) bridge = new ProgressBridge(progress, 0.02f, 0.55f);
        if (!BRepTools::Read(plainShape, inputPath.c_str(), builder, bridge->Start()) || plainShape.IsNull())
            throw std::runtime_error("This BREP file could not be read");
        Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        st->AddShape(plainShape, false);
        haveDoc = true;
    } else {
        throw std::runtime_error("Unsupported CAD format: " + extIn);
    }
    result.msTransfer = lap();
    report(0.55f);

    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    TDF_LabelSequence roots;
    shapeTool->GetFreeShapes(roots);
    if (roots.IsEmpty()) throw std::runtime_error("The file contains no shapes");

    // Tessellate everything once (instances share their tessellation).
    TopoDS_Compound all;
    BRep_Builder bb;
    bb.MakeCompound(all);
    for (TDF_LabelSequence::Iterator it(roots); it.More(); it.Next()) {
        TopoDS_Shape s = XCAFDoc_ShapeTool::GetShape(it.Value());
        if (!s.IsNull()) bb.Add(all, s);
    }
    Bnd_Box box;
    BRepBndLib::Add(all, box);
    if (box.IsVoid()) throw std::runtime_error("The file contains no geometry");
    double diag = std::sqrt(box.SquareExtent());
    if (!(diag > 0)) diag = 1.0;
    auto sinceStart = [&]() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clockStart).count();
    };
    auto mesh = [&](double rel, double ang, float lo, float hi, bool allowDecrease = false) {
        IMeshTools_Parameters params;
        params.Deflection = std::max(1e-6, diag * rel);
        params.Angle = ang;
        params.InParallel = true;
        params.Relative = false;
        params.MinSize = params.Deflection * 0.1;
        params.AllowQualityDecrease = allowDecrease;
        Handle(ProgressBridge) bridge = new ProgressBridge(progress, lo, hi);
        BRepMesh_IncrementalMesh mesher(all, params, bridge->Start());
        if (bridge->cancelled()) throw std::runtime_error("cancelled");
    };

    // Q-Plus: kaba geçiş. Büyük dosyada model birkaç saniye erken görünür.
    if (options.coarseRelativeDeflection > 0 && !options.coarseOutputPath.empty()) {
        mesh(options.coarseRelativeDeflection, options.coarseAngularDeflection, 0.55f, 0.65f);
        long long tris = 0;
        int n = writeParts(doc, options.coarseOutputPath, tris);
        result.msCoarse = lap();
        result.msFirstVisible = sinceStart();
        if (onMesh) onMesh({options.coarseOutputPath, n, tris, true, result.msFirstVisible});
    }

    mesh(options.relativeDeflection, options.angularDeflection, 0.65f, 0.85f);
    result.msMesh = lap();
    result.parts = writeParts(doc, outputPath, result.triangles);
    result.fullTriangles = result.triangles;
    // Q-Plus: telefon için büyükse kademeli sadeleştir; ağ dosyası yeniden yazılır.
    if (options.maxTriangles > 0 && result.triangles > options.maxTriangles) {
        result.simplified = true;
        const double steps[][2] = {{0.0022, 0.5}, {0.005, 0.7}, {0.012, 0.9}};
        for (const auto& st : steps) {
            mesh(st[0], st[1], 0.85f, 0.88f, true);
            result.parts = writeParts(doc, outputPath, result.triangles);
            if (result.triangles <= options.maxTriangles) break;
        }
    }
    if (options.rejectAboveTriangles > 0 && result.triangles > options.rejectAboveTriangles)
        throw std::runtime_error("The model is too large for this device");
    result.msWrite = lap();
    if (result.msFirstVisible == 0) result.msFirstVisible = sinceStart();
    if (onMesh) onMesh({outputPath, result.parts, result.triangles, false, sinceStart(), result.simplified, result.fullTriangles});
    report(0.9f);

    // Q-Plus: tam ölçüler en son; model bu sırada ekranda.
    if (options.computeMetrics) result.metrics = qpcad::computeMetrics(all);
    result.msMetrics = lap();
    (void)haveDoc;
    app->Close(doc);
    report(1.f);
    return result;
}

}  // namespace

CadResult importCad(const std::string& inputPath, const std::string& ext, const std::string& outputPath,
                    const CadOptions& options, const std::function<bool(float)>& progress,
                    const std::function<void(const MeshReady&)>& onMesh) {
    if (lower(ext) == "stl") return importStl(inputPath, outputPath, options, progress, onMesh);
    // Q-Plus: yalnız bu çağrı süresince; sonra sistem varsayılanına döner.
    struct SignalScope {
        bool on;
        explicit SignalScope(bool b) : on(b) { if (on) OSD::SetSignal(OSD_SignalMode_Set, false); }
        ~SignalScope() { if (on) OSD::SetSignal(OSD_SignalMode_Unset, false); }
    } signalScope(options.convertSignals);
    try {
        OCC_CATCH_SIGNALS
        return importImpl(inputPath, ext, outputPath, options, progress, onMesh);
    } catch (const Standard_Failure& f) {
        throw std::runtime_error(std::string("CAD kernel error: ") + (f.GetMessageString() ? f.GetMessageString() : "unknown"));
    }
}

}  // namespace fsvcad
