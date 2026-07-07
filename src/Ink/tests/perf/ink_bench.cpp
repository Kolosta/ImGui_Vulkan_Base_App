// ink_bench — the headless Ink performance harness (docs/Ink/PERF_TESTING.md).
//
// Creates its own Vulkan 1.3 device (no window, no ImGui, no SDL), runs the
// engine through the exact same render graph as the app (texture hooks left
// null — the display image simply is never sampled by a UI), and reports the
// Stats counters as JSON.
//
//   ink_bench [--scene bootstrap|steady|empty|paths_10k] [--frames N]
//             [--warmup N] [--shaders DIR] [--out FILE]
//
// Scenes (grow with the lots — docs/Ink/PERF_TESTING.md §3):
//   bootstrap — the demo document, camera fit, dirtied every frame
//               (simulated navigation → full re-record each frame)
//   steady    — same content, camera frozen → the steady-state short-circuit
//               must keep viewsRendered at 0 (record cost ≈ 0)
//   empty     — camera far off-content (hardware-clipped): frame-loop floor
//   paths_10k — 10 000 random filled+stroked Bézier blobs (deterministic
//               seed), camera fit, dirtied every frame

#include <Ink/Document/Document.h>
#include <Ink/Render/Renderer.h>

#include <vulkan/vulkan.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct BenchDevice {
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         device   = VK_NULL_HANDLE;
    VkQueue          queue    = VK_NULL_HANDLE;
    std::uint32_t    queueFamily = 0;
};

bool CreateBenchDevice(BenchDevice& d) {
    VkApplicationInfo app{};
    app.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ink_bench";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{};
    ici.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &d.instance) != VK_SUCCESS) {
        std::fprintf(stderr, "ink_bench: vkCreateInstance failed\n");
        return false;
    }

    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(d.instance, &count, nullptr);
    if (count == 0) { std::fprintf(stderr, "ink_bench: no Vulkan device\n"); return false; }
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(d.instance, &count, gpus.data());
    // Prefer a discrete GPU, else take the first.
    d.physical = gpus[0];
    for (VkPhysicalDevice g : gpus) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(g, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { d.physical = g; break; }
    }

    // A graphics queue family (no present needed — headless).
    vkGetPhysicalDeviceQueueFamilyProperties(d.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(d.physical, &count, families.data());
    bool found = false;
    for (std::uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { d.queueFamily = i; found = true; break; }
    if (!found) { std::fprintf(stderr, "ink_bench: no graphics queue\n"); return false; }

    // The engine's Vulkan 1.3 baseline (same as the app's device).
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = d.queueFamily;
    qi.queueCount       = 1;
    qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                = &f12;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &qi;
    if (vkCreateDevice(d.physical, &dci, nullptr, &d.device) != VK_SUCCESS) {
        std::fprintf(stderr, "ink_bench: vkCreateDevice failed (Vulkan 1.3 features?)\n");
        return false;
    }
    vkGetDeviceQueue(d.device, d.queueFamily, 0, &d.queue);
    return true;
}

void DestroyBenchDevice(BenchDevice& d) {
    if (d.device)   vkDestroyDevice(d.device, nullptr);
    if (d.instance) vkDestroyInstance(d.instance, nullptr);
    d = {};
}

// 10 000 random filled+stroked Bézier blobs over an 8000×4500 page —
// deterministic (LCG seed), built through the public Document API
// (docs/Ink/PERF_TESTING.md §3: scenes double as API integration tests).
void BuildPaths10k(Ink::Document& doc) {
    const Ink::NodeId page = doc.AddPage("Bench", { 0, 0 }, { 8000, 4500 });
    std::uint64_t lcg = 0x1234ABCDu;
    auto rnd = [&]() {   // [0,1)
        lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
        return (double)(lcg >> 11) / 9007199254740992.0;
    };
    constexpr double kTau = 6.28318530717958647692;
    for (int n = 0; n < 10000; ++n) {
        const double cx = 100.0 + rnd() * 7800.0;
        const double cy = 100.0 + rnd() * 4300.0;
        const double r  = 20.0 + rnd() * 40.0;
        // A smooth 6-anchor blob: points on a noisy circle, tangent handles.
        Ink::PathData path;
        Ink::Subpath sp;
        sp.closed = true;
        for (int i = 0; i < 6; ++i) {
            const double a  = kTau * (double)i / 6.0;
            const double rr = r * (0.65 + rnd() * 0.7);
            Ink::Anchor an;
            an.pos = { std::cos(a) * rr, std::sin(a) * rr };
            const double hl = rr * 0.55;
            an.out = { -std::sin(a) * hl,  std::cos(a) * hl };
            an.in  = {  std::sin(a) * hl, -std::cos(a) * hl };
            an.hasIn = an.hasOut = true;
            an.kind = Ink::AnchorKind::Smooth;
            sp.anchors.push_back(an);
        }
        path.subpaths.push_back(std::move(sp));

        Ink::Style style = Ink::Style::Filled(
            { (float)rnd(), (float)rnd(), (float)rnd(),
              0.35f + (float)rnd() * 0.6f });
        if (n % 2 == 0)
            style.WithStroke({ 0.05f, 0.05f, 0.08f, 1.0f }, 2.0 + rnd() * 4.0);
        const Ink::NodeId id = doc.AddPath(page, std::move(path), style, "blob");
        Ink::Transform2D t;
        t.tx = cx; t.ty = cy;
        t.rotation = rnd() * kTau;
        doc.SetTransform(id, t);
    }
}

struct Series {
    std::vector<double> values;
    void   Add(double v) { values.push_back(v); }
    double Avg() const {
        if (values.empty()) return 0.0;
        double s = 0.0; for (double v : values) s += v;
        return s / (double)values.size();
    }
    double Percentile(double p) const {
        if (values.empty()) return 0.0;
        std::vector<double> s = values;
        std::sort(s.begin(), s.end());
        const auto i = (std::size_t)(p * (double)(s.size() - 1) + 0.5);
        return s[std::min(i, s.size() - 1)];
    }
};

} // namespace

int main(int argc, char** argv) {
    std::string scene   = "bootstrap";
    std::string shaders = INK_BENCH_SHADER_DIR;
    std::string outPath;
    int frames = 300, warmup = 60;
    for (int i = 1; i < argc - 1; ++i) {
        if (!std::strcmp(argv[i], "--scene"))   scene   = argv[++i];
        else if (!std::strcmp(argv[i], "--frames"))  frames  = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--warmup"))  warmup  = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--shaders")) shaders = argv[++i];
        else if (!std::strcmp(argv[i], "--out"))     outPath = argv[++i];
    }
    if (scene != "bootstrap" && scene != "steady" && scene != "empty" &&
        scene != "paths_10k") {
        std::fprintf(stderr, "ink_bench: unknown scene '%s'\n", scene.c_str());
        return 2;
    }

    BenchDevice bd;
    if (!CreateBenchDevice(bd)) { DestroyBenchDevice(bd); return 1; }

    Ink::Renderer renderer;
    Ink::Renderer::InitInfo ii;
    ii.instance       = bd.instance;
    ii.physicalDevice = bd.physical;
    ii.device         = bd.device;
    ii.queue          = bd.queue;
    ii.queueFamily    = bd.queueFamily;
    ii.shaderDir      = shaders;
    // No texture hooks: headless — the display image never meets a UI.
    if (!renderer.Initialize(ii)) {
        std::fprintf(stderr, "ink_bench: engine init failed (shaders at '%s'?)\n",
                     shaders.c_str());
        DestroyBenchDevice(bd);
        return 1;
    }

    // The document under test (the engine renders the app-owned model).
    Ink::Document doc;
    if (scene == "paths_10k") BuildPaths10k(doc);
    else                      Ink::SeedDemoDocument(doc);
    renderer.SetDocument(&doc);

    constexpr std::uint32_t kW = 1920, kH = 1080;
    const int viewKey = 0;

    // Prime frame: compiles the scene (bounds become known) and pays the full
    // first build (compile + geometry + uploads) — reported as firstBuild.
    Ink::Stats firstBuild{};
    {
        renderer.BeginFrame();
        Ink::View* v = renderer.AcquireView(&viewKey);
        v->SetViewport(kW, kH);
        v->SetCamera(0.0, 0.0, 1.0);
        v->SetBackground(Ink::SrgbToLinearPremultiplied(0.16f, 0.16f, 0.18f, 1.0f));
        renderer.EndFrame();
        firstBuild = renderer.GetStats();
    }

    const Ink::Rect b = renderer.SceneBounds();
    const double fitZoom = std::min((double)kW / b.Width(), (double)kH / b.Height()) * 0.94;
    double panX = b.min.x + b.Width() * 0.5 - kW * 0.5 / fitZoom;
    double panY = b.min.y + b.Height() * 0.5 - kH * 0.5 / fitZoom;
    if (scene == "empty") { panX += 100000.0; panY += 100000.0; }
    const bool dirtyEachFrame = (scene != "steady");

    Series frameMs, recordMs, gpuMs, geomMs, syncMs;
    Ink::Stats last{};
    std::uint64_t steadySkips = 0;

    const int total = warmup + frames;
    for (int f = 0; f < total; ++f) {
        const auto t0 = std::chrono::steady_clock::now();
        renderer.BeginFrame();
        Ink::View* v = renderer.AcquireView(&viewKey);
        v->SetViewport(kW, kH);
        // Simulated navigation: a sub-pixel pan step defeats the signature so
        // the full record path is measured every frame.
        const double wobble = dirtyEachFrame ? 0.01 * (double)f : 0.0;
        v->SetCamera(panX + wobble, panY, fitZoom);
        v->SetBackground(Ink::SrgbToLinearPremultiplied(0.16f, 0.16f, 0.18f, 1.0f));
        renderer.EndFrame();
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();

        if (f >= warmup) {
            last = renderer.GetStats();
            frameMs.Add(ms);
            recordMs.Add(last.recordMs);
            geomMs.Add(last.geomMs);
            syncMs.Add(last.syncMs);
            if (last.gpuMs > 0.0f) gpuMs.Add(last.gpuMs);
            if (last.viewsRendered == 0) ++steadySkips;
        }
    }
    vkDeviceWaitIdle(bd.device);

    char json[2560];
    std::snprintf(json, sizeof json,
        "{\n"
        "  \"schema\": 1,\n"
        "  \"scene\": \"%s\",\n"
        "  \"frames\": %d,\n"
        "  \"viewport\": [%u, %u],\n"
        "  \"firstBuild\": { \"compileMs\": %.4f, \"geomMs\": %.4f,\n"
        "                    \"syncMs\": %.4f, \"recordMs\": %.4f },\n"
        "  \"metrics\": {\n"
        "    \"frameMs\":  { \"avg\": %.4f, \"p50\": %.4f, \"p99\": %.4f },\n"
        "    \"recordMs\": { \"avg\": %.4f, \"p99\": %.4f },\n"
        "    \"geomMs\":   { \"avg\": %.4f, \"p99\": %.4f },\n"
        "    \"syncMs\":   { \"avg\": %.4f, \"p99\": %.4f },\n"
        "    \"gpuMs\":    { \"avg\": %.4f, \"p99\": %.4f },\n"
        "    \"counters\": { \"drawCalls\": %u, \"triangles\": %u,\n"
        "                    \"instances\": %u, \"steadySkippedFrames\": %llu }\n"
        "  }\n"
        "}\n",
        scene.c_str(), frames, kW, kH,
        firstBuild.compileMs, firstBuild.geomMs, firstBuild.syncMs,
        firstBuild.recordMs,
        frameMs.Avg(), frameMs.Percentile(0.5), frameMs.Percentile(0.99),
        recordMs.Avg(), recordMs.Percentile(0.99),
        geomMs.Avg(), geomMs.Percentile(0.99),
        syncMs.Avg(), syncMs.Percentile(0.99),
        gpuMs.Avg(), gpuMs.Percentile(0.99),
        last.drawCalls, last.triangles, last.instances,
        (unsigned long long)steadySkips);

    std::printf("%s", json);
    if (!outPath.empty()) {
        if (std::FILE* out = std::fopen(outPath.c_str(), "wb")) {
            std::fputs(json, out);
            std::fclose(out);
        } else {
            std::fprintf(stderr, "ink_bench: cannot write '%s'\n", outPath.c_str());
        }
    }

    renderer.Shutdown();
    DestroyBenchDevice(bd);
    return 0;
}
