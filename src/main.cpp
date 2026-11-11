// Vogelflug - Flug ueber eine verlassene, von der Natur zurueckeroberte Stadt.
// Eigene Vulkan-Engine, komplettes Bild per Hardware-Raytracing (VK_KHR_ray_query):
// Primaerstrahlen, Sonnen-/Mondschatten, Global Illumination, Spiegelungen, volumetrischer Nebel.
#define NOMINMAX
#include <volk.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "world.h"
#include "shaders.h"
#include "font.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
extern "C" { // auf Notebooks die dedizierte GPU anfordern
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

static const float PI = 3.14159265f;

// ------------------------------------------------------------------ Log / Fehler
static FILE* gLog = nullptr;
static void logf(const char* fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    fprintf(stderr, "%s\n", buf);
    if (gLog) { fprintf(gLog, "%s\n", buf); fflush(gLog); }
}
[[noreturn]] static void fatal(const char* fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    logf("FEHLER: %s", buf);
#ifdef _WIN32
    MessageBoxA(nullptr, buf, "Vogelflug - Fehler", MB_OK | MB_ICONERROR);
#endif
    exit(1);
}
#define VKC(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) fatal("Vulkan-Fehler %d bei: %s (Zeile %d)", (int)r_, #x, __LINE__); } while (0)

// ------------------------------------------------------------------ Vulkan-Grundobjekte
struct Buffer { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; VkDeviceSize size = 0; void* map = nullptr; VkDeviceAddress addr = 0; };
struct Image { VkImage i = VK_NULL_HANDLE; VkImageView v = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; uint32_t w = 0, h = 0; VkFormat f = VK_FORMAT_UNDEFINED; };
struct Pass { VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout pl = VK_NULL_HANDLE; VkPipeline pipe = VK_NULL_HANDLE; };
struct DS { // eine Descriptor-Bindung
    VkDescriptorType type; VkImageView view = VK_NULL_HANDLE; VkSampler samp = VK_NULL_HANDLE; VkBuffer buf = VK_NULL_HANDLE; VkAccelerationStructureKHR as = VK_NULL_HANDLE;
};

struct FrameUBO {
    float camPos[4], camRight[4], camUp[4], camFwd[4], pCamPos[4], pCamRight[4], pCamUp[4], pCamFwd[4];
    float sunDir[4], sunColor[4], moonDir[4], moonColor[4], lightDir[4], lightColor[4], sky[4], fog[4];
    float res[4], outRes[4], jitter[4], birdDelta[4], misc[4], misc2[4], misc3[4];
};
static void set4(float* d, vec3 v, float w = 0) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = w; }

struct Options {
    bool windowed = false, validate = false, vsync = true, move = false;
    int width = 1600, height = 900, quality = -1, shotFrames = 0, camMode = 0;
    float density = 1.0f, hour = 7.75f, yawDeg = 180, pitchDeg = 0, clouds = 0.3f, orbitDeg = 0, orbitPitch = 0, flapPhase = -1;
    bool hasPos = false, nohud = false; vec3 pos; std::string shot;
};

struct Quality { const char* name; float scale; int fogSteps; int gi; };
static const Quality QUALITY[4] = {{"Leistung", 0.6f, 2, 1}, {"Ausgewogen", 0.77f, 3, 1}, {"Hoch", 1.0f, 4, 1}, {"Ultra", 1.0f, 8, 2}};

struct App {
    Options opt;
    GLFWwindow* win = nullptr;
    VkInstance inst = VK_NULL_HANDLE; VkPhysicalDevice pd = VK_NULL_HANDLE; VkDevice dev = VK_NULL_HANDLE;
    uint32_t qf = 0; VkQueue queue = VK_NULL_HANDLE; VkSurfaceKHR surf = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};
    VkSwapchainKHR sc = VK_NULL_HANDLE; std::vector<VkImage> scImgs; std::vector<VkImageView> scViews; VkFormat scFmt = VK_FORMAT_UNDEFINED; VkExtent2D scExt{}; bool scSrgb = false, scCanCopy = false;
    VkCommandPool pool = VK_NULL_HANDLE; VkCommandBuffer cmd = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE; VkSemaphore semAcq = VK_NULL_HANDLE, semDone = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkSampler sNearest = VK_NULL_HANDLE, sLinear = VK_NULL_HANDLE, sSky = VK_NULL_HANDLE;
    uint32_t scratchAlign = 256; uint64_t maxStorageRange = 0;
    char gpuName[256] = {0};

    // Szene
    World world;
    Buffer vb, ib, ubo, asStore[64], tlasInst, tlasBuf, birdInst, birdBuf, birdScratch, hudStage, shotBuf;
    Buffer birdVB, birdIB, birdBlasBuf, birdBlasScratch; VkAccelerationStructureKHR birdBlas = VK_NULL_HANDLE; VkDeviceAddress birdBlasAddr = 0; uint32_t birdTris = 0, birdVerts = 0;
    int nAsStore = 0;
    std::vector<VkAccelerationStructureKHR> blas; std::vector<VkDeviceAddress> blasAddr;
    struct MeshRange { uint32_t triBase, nOpaque, nLeaf, leafType; bool minor; };
    std::vector<MeshRange> ranges; // erst instanzierte Meshes, dann Chunks
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE, birdTlas = VK_NULL_HANDLE;
    uint64_t totalTris = 0; size_t nInstances = 0;

    // Renderziele
    uint32_t rw = 0, rh = 0;
    Image albedo, nd[2], direct, indirect, spec, fogI, motion, histD[2], histI[2], atA, atB, hdr, taa[2], bloomA, bloomB, skyImg, hud, expImg;
    Pass pSky, pRt, pTemporal, pAtrous, pComposite, pTaa, pBloom, pExposure, pFinal;
    VkDescriptorSet sSkySet = VK_NULL_HANDLE, sRt[2]{}, sTemporal[2]{}, sAtrous[2][3]{}, sComposite[2]{}, sTaa[2]{}, sBloom[2][3]{}, sExposure[2]{}, sFinal[2]{};
    uint64_t frame = 0; bool targetsDirty = false, swapDirty = false;

    // Spielzustand
    vec3 pos; float yaw = PI, pitch = 0, roll = 0, speed = 14, turn = 0, yawRate = 0, flap = 0, flapAmt = 1;
    float camYaw = PI, camPitch = 0, camDistF = 1;
    float hour = 7.75f, timeRate = 1.0f; bool timeRun = true; // timeRate: Spielstunden pro Echt-Minute
    float cloudCov = 0.3f, cloudTime = 0;
    int quality = 2, camMode = 0; bool paused = false, help = true, hudOn = true, fullscreen = true;
    double helpUntil = 18.0;
    double mdx = 0, mdy = 0, lastMx = 0, lastMy = 0; bool haveMouse = false; double scrollAcc = 0;
    bool keyPrev[GLFW_KEY_LAST + 1] = {false};
    float fps = 0; double gameTime = 0;
    vec3 camPos, camR, camU, camF; float tanX = 1, tanY = 1;
    vec3 pCamPos, pCamR, pCamU, pCamF; float pTanX = 1, pTanY = 1; bool havePrevCam = false;
    vec3 prevBirdPos; vec3 lastSkySun{0, 0, 0}; float lastSkyCov = -1;
    std::string hudText; bool hudDirty = true; double hudNext = 0;
    bool timeScrub = false;
    int winX = 100, winY = 100, winW = 1600, winH = 900;
};
static App A;

// ------------------------------------------------------------------ Hilfsfunktionen Vulkan
static uint32_t findMem(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < A.memProps.memoryTypeCount; i++) if ((bits & (1u << i)) && (A.memProps.memoryTypes[i].propertyFlags & want) == want) return i;
    fatal("Kein passender Grafikspeicher-Typ gefunden.");
}
static Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
    Buffer b; b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKC(vkCreateBuffer(A.dev, &ci, nullptr, &b.b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(A.dev, b.b, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMem(mr.memoryTypeBits, host ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) { fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; ai.pNext = &fi; }
    VkResult r = vkAllocateMemory(A.dev, &ai, nullptr, &b.m);
    if (r != VK_SUCCESS) fatal("Zu wenig Grafikspeicher (Anforderung %.0f MB fehlgeschlagen, Code %d).\nTipp: mit --density 0.5 starten.", double(mr.size) / 1048576.0, (int)r);
    VKC(vkBindBufferMemory(A.dev, b.b, b.m, 0));
    if (host) VKC(vkMapMemory(A.dev, b.m, 0, VK_WHOLE_SIZE, 0, &b.map));
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) { VkBufferDeviceAddressInfo di{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; di.buffer = b.b; b.addr = vkGetBufferDeviceAddress(A.dev, &di); }
    return b;
}
static void freeBuffer(Buffer& b) { if (b.b) vkDestroyBuffer(A.dev, b.b, nullptr); if (b.m) vkFreeMemory(A.dev, b.m, nullptr); b = Buffer(); }
static Image makeImage(uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags usage) {
    Image im; im.w = w; im.h = h; im.f = f;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType = VK_IMAGE_TYPE_2D; ci.format = f; ci.extent = {w, h, 1}; ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKC(vkCreateImage(A.dev, &ci, nullptr, &im.i));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(A.dev, im.i, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size; ai.memoryTypeIndex = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VKC(vkAllocateMemory(A.dev, &ai, nullptr, &im.m)); VKC(vkBindImageMemory(A.dev, im.i, im.m, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = im.i; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = f; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VKC(vkCreateImageView(A.dev, &vi, nullptr, &im.v));
    return im;
}
static void freeImage(Image& im) { if (im.v) vkDestroyImageView(A.dev, im.v, nullptr); if (im.i) vkDestroyImage(A.dev, im.i, nullptr); if (im.m) vkFreeMemory(A.dev, im.m, nullptr); im = Image(); }
static VkCommandBuffer beginOnce() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = A.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer c; VKC(vkAllocateCommandBuffers(A.dev, &ai, &c));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; VKC(vkBeginCommandBuffer(c, &bi));
    return c;
}
static void endOnce(VkCommandBuffer c) {
    VKC(vkEndCommandBuffer(c));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &c;
    VKC(vkQueueSubmit(A.queue, 1, &si, VK_NULL_HANDLE)); VKC(vkQueueWaitIdle(A.queue));
    vkFreeCommandBuffers(A.dev, A.pool, 1, &c);
}
static void imgBarrier(VkCommandBuffer c, VkImage img, VkImageLayout a, VkImageLayout b, VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkImageMemoryBarrier mb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; mb.oldLayout = a; mb.newLayout = b; mb.srcAccessMask = sa; mb.dstAccessMask = da;
    mb.srcQueueFamilyIndex = mb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; mb.image = img; mb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(c, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &mb);
}
static void memBarrier(VkCommandBuffer c, VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = sa; mb.dstAccessMask = da;
    vkCmdPipelineBarrier(c, ss, ds, 0, 1, &mb, 0, nullptr, 0, nullptr);
}
static VkShaderModule makeShader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m; VKC(vkCreateShaderModule(A.dev, &ci, nullptr, &m)); return m;
}
static void makeLayout(Pass& p, const std::vector<VkDescriptorType>& types, VkShaderStageFlags stages, uint32_t pushBytes) {
    std::vector<VkDescriptorSetLayoutBinding> b(types.size());
    for (uint32_t i = 0; i < types.size(); i++) { b[i] = {}; b[i].binding = i; b[i].descriptorType = types[i]; b[i].descriptorCount = 1; b[i].stageFlags = stages; }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount = (uint32_t)b.size(); li.pBindings = b.data();
    VKC(vkCreateDescriptorSetLayout(A.dev, &li, nullptr, &p.dsl));
    VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pi.setLayoutCount = 1; pi.pSetLayouts = &p.dsl;
    VkPushConstantRange pr{stages, 0, pushBytes}; if (pushBytes) { pi.pushConstantRangeCount = 1; pi.pPushConstantRanges = &pr; }
    VKC(vkCreatePipelineLayout(A.dev, &pi, nullptr, &p.pl));
}
static Pass makeCompute(const uint32_t* code, size_t bytes, const std::vector<VkDescriptorType>& types, uint32_t pushBytes = 0) {
    Pass p; makeLayout(p, types, VK_SHADER_STAGE_COMPUTE_BIT, pushBytes);
    VkShaderModule sm = makeShader(code, bytes);
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; ci.layout = p.pl;
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = sm; ci.stage.pName = "main";
    VKC(vkCreateComputePipelines(A.dev, VK_NULL_HANDLE, 1, &ci, nullptr, &p.pipe));
    vkDestroyShaderModule(A.dev, sm, nullptr);
    return p;
}
static VkDescriptorSet makeSet(const Pass& p, const std::vector<DS>& b) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = A.dpool; ai.descriptorSetCount = 1; ai.pSetLayouts = &p.dsl;
    VkDescriptorSet s; VKC(vkAllocateDescriptorSets(A.dev, &ai, &s));
    std::vector<VkWriteDescriptorSet> w(b.size()); std::vector<VkDescriptorImageInfo> ii(b.size()); std::vector<VkDescriptorBufferInfo> bi(b.size());
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> ai2(b.size());
    for (uint32_t i = 0; i < b.size(); i++) {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w[i].dstSet = s; w[i].dstBinding = i; w[i].descriptorCount = 1; w[i].descriptorType = b[i].type;
        if (b[i].type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
            ai2[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR}; ai2[i].accelerationStructureCount = 1; ai2[i].pAccelerationStructures = &b[i].as; w[i].pNext = &ai2[i];
        } else if (b[i].buf) { bi[i] = {b[i].buf, 0, VK_WHOLE_SIZE}; w[i].pBufferInfo = &bi[i]; }
        else { ii[i] = {b[i].samp, b[i].view, VK_IMAGE_LAYOUT_GENERAL}; w[i].pImageInfo = &ii[i]; }
    }
    vkUpdateDescriptorSets(A.dev, (uint32_t)w.size(), w.data(), 0, nullptr);
    return s;
}
static DS dUbo() { DS d{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}; d.buf = A.ubo.b; return d; }
static DS dImg(const Image& im) { DS d{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}; d.view = im.v; return d; }
static DS dTex(const Image& im, VkSampler s) { DS d{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}; d.view = im.v; d.samp = s; return d; }
static DS dBuf(const Buffer& b) { DS d{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}; d.buf = b.b; return d; }
static DS dAs(VkAccelerationStructureKHR a) { DS d{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}; d.as = a; return d; }

// ------------------------------------------------------------------ Geraet
static bool hasExt(const std::vector<VkExtensionProperties>& e, const char* n) { for (auto& x : e) if (!strcmp(x.extensionName, n)) return true; return false; }
static const char* DEV_EXT[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};

static void initVulkan() {
    if (volkInitialize() != VK_SUCCESS) fatal("Vulkan wurde nicht gefunden.\nBitte einen aktuellen Grafiktreiber installieren (NVIDIA GeForce-Treiber).");
    uint32_t nExt = 0; const char** glfwExt = glfwGetRequiredInstanceExtensions(&nExt);
    if (!glfwExt) fatal("Dieses System unterstuetzt keine Vulkan-Fensterausgabe.");
    std::vector<const char*> exts(glfwExt, glfwExt + nExt), layers;
    if (A.opt.validate) layers.push_back("VK_LAYER_KHRONOS_validation");
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName = "Vogelflug"; ai.pEngineName = "Vogelflug RT"; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; ici.enabledExtensionCount = (uint32_t)exts.size(); ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount = (uint32_t)layers.size(); ici.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ici, nullptr, &A.inst);
    if (r != VK_SUCCESS) fatal("Vulkan 1.3 konnte nicht initialisiert werden (Code %d).\nBitte den Grafiktreiber aktualisieren.", (int)r);
    volkLoadInstance(A.inst);
    VKC(glfwCreateWindowSurface(A.inst, A.win, nullptr, &A.surf));

    uint32_t n = 0; vkEnumeratePhysicalDevices(A.inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(A.inst, &n, pds.data());
    int bestScore = -1; std::string found;
    for (auto pd : pds) {
        VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(pd, &pr);
        found += std::string("\n - ") + pr.deviceName;
        if (pr.apiVersion < VK_API_VERSION_1_3) { found += " (kein Vulkan 1.3)"; continue; }
        uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd, nullptr, &ne, nullptr); std::vector<VkExtensionProperties> de(ne); vkEnumerateDeviceExtensionProperties(pd, nullptr, &ne, de.data());
        bool ok = true; for (auto e : DEV_EXT) ok = ok && hasExt(de, e);
        if (!ok) { found += " (kein Raytracing)"; continue; }
        VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq};
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &as};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        if (!rq.rayQuery || !as.accelerationStructure || !f12.bufferDeviceAddress || !f13.dynamicRendering) { found += " (Raytracing-Features fehlen)"; continue; }
        uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr); std::vector<VkQueueFamilyProperties> qp(nq); vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qp.data());
        int q = -1;
        for (uint32_t i = 0; i < nq; i++) { VkBool32 pres = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, A.surf, &pres);
            if (pres && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) { q = (int)i; break; } }
        if (q < 0) continue;
        int score = pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 100 : 10);
        if (score > bestScore) { bestScore = score; A.pd = pd; A.qf = (uint32_t)q; snprintf(A.gpuName, sizeof A.gpuName, "%s", pr.deviceName); }
    }
    if (bestScore < 0) fatal("Keine Grafikkarte mit Hardware-Raytracing (VK_KHR_ray_query) gefunden.\nBenoetigt wird z.B. eine NVIDIA RTX-Karte mit aktuellem Treiber.\n\nGefundene Geraete:%s", found.c_str());
    logf("Grafikkarte: %s", A.gpuName);
    vkGetPhysicalDeviceMemoryProperties(A.pd, &A.memProps);
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asp}; vkGetPhysicalDeviceProperties2(A.pd, &p2);
    A.scratchAlign = std::max(asp.minAccelerationStructureScratchOffsetAlignment, 16u);
    A.maxStorageRange = p2.properties.limits.maxStorageBufferRange;

    float prio = 1.0f; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = A.qf; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR}; rq.rayQuery = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq}; as.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &as}; f13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13}; f12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext = &f2; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qi;
    dci.enabledExtensionCount = 4; dci.ppEnabledExtensionNames = DEV_EXT;
    VKC(vkCreateDevice(A.pd, &dci, nullptr, &A.dev));
    volkLoadDevice(A.dev);
    vkGetDeviceQueue(A.dev, A.qf, 0, &A.queue);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = A.qf;
    VKC(vkCreateCommandPool(A.dev, &pci, nullptr, &A.pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = A.pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VKC(vkAllocateCommandBuffers(A.dev, &cai, &A.cmd));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT; VKC(vkCreateFence(A.dev, &fci, nullptr, &A.fence));
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VKC(vkCreateSemaphore(A.dev, &sci, nullptr, &A.semAcq)); VKC(vkCreateSemaphore(A.dev, &sci, nullptr, &A.semDone));

    VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16}, {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 16}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 64; dpi.poolSizeCount = 5; dpi.pPoolSizes = ps;
    VKC(vkCreateDescriptorPool(A.dev, &dpi, nullptr, &A.dpool));

    auto mkSampler = [](VkFilter f, VkSamplerAddressMode u, VkSamplerAddressMode v) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.magFilter = si.minFilter = f; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = u; si.addressModeV = v; si.addressModeW = v; si.maxLod = 0;
        VkSampler s; VKC(vkCreateSampler(A.dev, &si, nullptr, &s)); return s; };
    A.sNearest = mkSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    A.sLinear = mkSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    A.sSky = mkSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    A.ubo = makeBuffer(sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
}

static void destroySwapchainViews() { for (auto v : A.scViews) vkDestroyImageView(A.dev, v, nullptr); A.scViews.clear(); }
static bool createSwapchain() {
    VkSurfaceCapabilitiesKHR caps; VKC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(A.pd, A.surf, &caps));
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) { int w, h; glfwGetFramebufferSize(A.win, &w, &h); ext.width = (uint32_t)w; ext.height = (uint32_t)h; }
    ext.width = std::min(std::max(ext.width, caps.minImageExtent.width), caps.maxImageExtent.width);
    ext.height = std::min(std::max(ext.height, caps.minImageExtent.height), caps.maxImageExtent.height);
    if (ext.width == 0 || ext.height == 0) return false;
    uint32_t n = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(A.pd, A.surf, &n, nullptr); std::vector<VkSurfaceFormatKHR> fm(n); vkGetPhysicalDeviceSurfaceFormatsKHR(A.pd, A.surf, &n, fm.data());
    VkSurfaceFormatKHR sf = fm[0];
    for (auto& f : fm) if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { sf = f; break; }
    A.scSrgb = (sf.format == VK_FORMAT_B8G8R8A8_SRGB || sf.format == VK_FORMAT_R8G8B8A8_SRGB || sf.format == VK_FORMAT_A8B8G8R8_SRGB_PACK32);
    vkGetPhysicalDeviceSurfacePresentModesKHR(A.pd, A.surf, &n, nullptr); std::vector<VkPresentModeKHR> pm(n); vkGetPhysicalDeviceSurfacePresentModesKHR(A.pd, A.surf, &n, pm.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!A.opt.vsync) { bool mb = false, im = false; for (auto m : pm) { mb |= (m == VK_PRESENT_MODE_MAILBOX_KHR); im |= (m == VK_PRESENT_MODE_IMMEDIATE_KHR); } if (mb) mode = VK_PRESENT_MODE_MAILBOX_KHR; else if (im) mode = VK_PRESENT_MODE_IMMEDIATE_KHR; }
    else for (auto m : pm) if (m == VK_PRESENT_MODE_FIFO_RELAXED_KHR) mode = m; // VSync, aber ohne FPS-Halbierung bei knapper Leistung
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; ci.surface = A.surf;
    ci.minImageCount = std::max(caps.minImageCount + 1, 3u); if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = sf.format; ci.imageColorSpace = sf.colorSpace; ci.imageExtent = ext; ci.imageArrayLayers = 1;
    A.scCanCopy = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (A.scCanCopy ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & ci.compositeAlpha)) for (uint32_t b = 1; b <= 8; b <<= 1) if (caps.supportedCompositeAlpha & b) { ci.compositeAlpha = (VkCompositeAlphaFlagBitsKHR)b; break; }
    ci.presentMode = mode; ci.clipped = VK_TRUE; ci.oldSwapchain = A.sc;
    VkSwapchainKHR nsc; VKC(vkCreateSwapchainKHR(A.dev, &ci, nullptr, &nsc));
    destroySwapchainViews(); if (A.sc) vkDestroySwapchainKHR(A.dev, A.sc, nullptr);
    A.sc = nsc; A.scFmt = sf.format; A.scExt = ext;
    vkGetSwapchainImagesKHR(A.dev, A.sc, &n, nullptr); A.scImgs.resize(n); vkGetSwapchainImagesKHR(A.dev, A.sc, &n, A.scImgs.data());
    for (auto img : A.scImgs) { VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = A.scFmt; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v; VKC(vkCreateImageView(A.dev, &vi, nullptr, &v)); A.scViews.push_back(v); }
    return true;
}

// ------------------------------------------------------------------ Geometrie + Beschleunigungsstrukturen
// Der Vogel hat eigene (vom Host beschreibbare) Vertex-/Indexpuffer und ein eigenes BLAS, das jedes Frame
// aus dem verformten Netz neu gebaut wird.
static void birdBlasInfo(VkAccelerationStructureGeometryKHR& g, VkAccelerationStructureBuildGeometryInfoKHR& info) {
    g = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR}; g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR; g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& t = g.geometry.triangles; t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR; t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = A.birdVB.addr; t.vertexStride = sizeof(Vtx); t.maxVertex = A.birdVerts - 1; t.indexType = VK_INDEX_TYPE_UINT32; t.indexData.deviceAddress = A.birdIB.addr;
    info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR}; info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR; info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; info.geometryCount = 1; info.pGeometries = &g;
}
static void buildScene() {
    World& W = A.world;
    uint64_t nv = 0, ni = 0;
    auto count = [&](const MeshB& m) { nv += m.v.size(); ni += m.io.size() + m.il.size(); };
    for (auto& m : W.meshes) count(m);
    for (auto& m : W.chunks) count(m);
    A.totalTris = ni / 3;
    if (ni / 3 >= (1u << 24)) fatal("Zu viele Dreiecke (%llu). Bitte --density verringern.", (unsigned long long)(ni / 3));
    logf("Geometrie: %.2f Mio. Dreiecke, %.2f Mio. Vertices, %zu Instanzen", ni / 3e6, nv / 1e6, W.insts.size());
    if (nv * sizeof(Vtx) > A.maxStorageRange) logf("Hinweis: Vertexpuffer (%.0f MB) ist groesser als das vom Treiber garantierte Limit (%.0f MB).", nv * sizeof(Vtx) / 1048576.0, A.maxStorageRange / 1048576.0);
    Buffer sv = makeBuffer(nv * sizeof(Vtx), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true), si = makeBuffer(ni * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    Vtx* pv = (Vtx*)sv.map; uint32_t* pi = (uint32_t*)si.map; uint64_t cv = 0, cidx = 0;
    auto add = [&](MeshB& m) {
        App::MeshRange r; r.triBase = (uint32_t)(cidx / 3); r.nOpaque = (uint32_t)m.io.size() / 3; r.nLeaf = (uint32_t)m.il.size() / 3; r.leafType = m.leafType; r.minor = m.minor;
        if (r.nOpaque >= (1u << 20)) fatal("Chunk zu gross.");
        if (!m.v.empty()) memcpy(pv + cv, m.v.data(), m.v.size() * sizeof(Vtx));
        for (uint32_t x : m.io) pi[cidx++] = x + (uint32_t)cv;
        for (uint32_t x : m.il) pi[cidx++] = x + (uint32_t)cv;
        cv += m.v.size(); A.ranges.push_back(r);
        m.v.clear(); m.v.shrink_to_fit(); m.io.clear(); m.io.shrink_to_fit(); m.il.clear(); m.il.shrink_to_fit();
    };
    for (auto& m : W.meshes) add(m);
    for (auto& m : W.chunks) add(m);
    const VkBufferUsageFlags geoUse = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    A.vb = makeBuffer(nv * sizeof(Vtx), geoUse, false); A.ib = makeBuffer(ni * 4, geoUse, false);
    { VkCommandBuffer c = beginOnce(); VkBufferCopy r1{0, 0, nv * sizeof(Vtx)}, r2{0, 0, ni * 4}; vkCmdCopyBuffer(c, sv.b, A.vb.b, 1, &r1); vkCmdCopyBuffer(c, si.b, A.ib.b, 1, &r2); endOnce(c); }
    freeBuffer(sv); freeBuffer(si);

    // ---- BLAS: je Mesh/Chunk eine Struktur mit bis zu 2 Geometrien (0 = opak, 1 = Blattkarten mit Alpha-Test)
    size_t N = A.ranges.size();
    struct BB { VkAccelerationStructureGeometryKHR geo[2]; VkAccelerationStructureBuildRangeInfoKHR range[2]; uint32_t ng = 0; VkAccelerationStructureBuildGeometryInfoKHR info; VkAccelerationStructureBuildSizesInfoKHR sizes; int store = 0; VkDeviceSize offset = 0; };
    std::vector<BB> bb(N);
    A.blas.assign(N, VK_NULL_HANDLE); A.blasAddr.assign(N, 0);
    const VkDeviceSize PAGE = 256ull << 20;
    VkDeviceSize pageUsed = 0; std::vector<VkDeviceSize> pageSize;
    for (size_t k = 0; k < N; k++) {
        BB& b = bb[k]; const auto& r = A.ranges[k];
        auto addGeo = [&](uint32_t firstTri, uint32_t nTri, bool opaque) {
            VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR}; g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR; g.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
            auto& t = g.geometry.triangles; t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR; t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            t.vertexData.deviceAddress = A.vb.addr; t.vertexStride = sizeof(Vtx); t.maxVertex = (uint32_t)nv - 1; t.indexType = VK_INDEX_TYPE_UINT32; t.indexData.deviceAddress = A.ib.addr;
            b.geo[b.ng] = g; b.range[b.ng] = {nTri, firstTri * 12u, 0, 0}; b.ng++;
        };
        if (r.nOpaque) addGeo(r.triBase, r.nOpaque, true);
        if (r.nLeaf) addGeo(r.triBase + r.nOpaque, r.nLeaf, false);
        if (!b.ng) continue;
        b.info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR}; b.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        b.info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; b.info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; b.info.geometryCount = b.ng; b.info.pGeometries = b.geo;
        uint32_t cnt[2] = {b.range[0].primitiveCount, b.range[1].primitiveCount};
        b.sizes = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(A.dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &b.info, cnt, &b.sizes);
        VkDeviceSize sz = (b.sizes.accelerationStructureSize + 255) & ~VkDeviceSize(255);
        if (pageSize.empty() || pageUsed + sz > std::max(PAGE, sz)) { pageSize.push_back(0); pageUsed = 0; }
        b.store = (int)pageSize.size() - 1; b.offset = pageUsed; pageUsed += sz; pageSize.back() = pageUsed;
    }
    if (pageSize.size() > 64) fatal("Zu viel Geometrie fuer die Beschleunigungsstrukturen.");
    VkDeviceSize asTotal = 0;
    for (size_t p = 0; p < pageSize.size(); p++) { A.asStore[p] = makeBuffer(pageSize[p], VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false); asTotal += pageSize[p]; }
    A.nAsStore = (int)pageSize.size();
    VkDeviceSize maxScratch = 0;
    for (size_t k = 0; k < N; k++) { BB& b = bb[k]; if (!b.ng) continue;
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR}; ci.buffer = A.asStore[b.store].b; ci.offset = b.offset; ci.size = b.sizes.accelerationStructureSize; ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VKC(vkCreateAccelerationStructureKHR(A.dev, &ci, nullptr, &A.blas[k]));
        VkAccelerationStructureDeviceAddressInfoKHR di{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR}; di.accelerationStructure = A.blas[k]; A.blasAddr[k] = vkGetAccelerationStructureDeviceAddressKHR(A.dev, &di);
        b.info.dstAccelerationStructure = A.blas[k]; b.info.pGeometries = b.geo;
        maxScratch = std::max(maxScratch, b.sizes.buildScratchSize);
    }
    const VkDeviceSize SCR = std::max<VkDeviceSize>(96ull << 20, maxScratch + A.scratchAlign);
    Buffer scratch = makeBuffer(SCR, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
    auto alignUp = [&](VkDeviceSize v) { return (v + A.scratchAlign - 1) / A.scratchAlign * A.scratchAlign; };
    for (size_t k = 0; k < N;) {
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos; std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rr;
        VkDeviceSize used = alignUp(scratch.addr) - scratch.addr;
        for (; k < N; k++) { BB& b = bb[k]; if (!b.ng) continue;
            if (used + b.sizes.buildScratchSize > SCR) break;
            b.info.scratchData.deviceAddress = scratch.addr + used; used = alignUp(used + b.sizes.buildScratchSize);
            infos.push_back(b.info); rr.push_back(b.range); }
        if (infos.empty()) break;
        VkCommandBuffer c = beginOnce(); vkCmdBuildAccelerationStructuresKHR(c, (uint32_t)infos.size(), infos.data(), rr.data()); endOnce(c);
    }
    freeBuffer(scratch);

    // ---- TLAS der statischen Welt
    size_t nChunks = W.chunks.size(), nMesh = W.meshes.size();
    std::vector<VkAccelerationStructureInstanceKHR> inst; inst.reserve(W.insts.size() + nChunks);
    auto mk = [&](size_t rangeIdx, const float* m) {
        VkAccelerationStructureInstanceKHR i{}; memcpy(&i.transform, m, 48);
        const auto& r = A.ranges[rangeIdx]; i.instanceCustomIndex = r.triBase; i.mask = r.minor ? 0x02 : 0x01; i.instanceShaderBindingTableRecordOffset = r.nOpaque | (r.leafType << 20);
        i.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR; i.accelerationStructureReference = A.blasAddr[rangeIdx]; return i; };
    const float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (size_t c = 0; c < nChunks; c++) if (A.blas[nMesh + c]) inst.push_back(mk(nMesh + c, ident));
    for (auto& i : W.insts) if (A.blas[i.mesh]) inst.push_back(mk(i.mesh, i.m));
    A.nInstances = inst.size();
    auto buildTlas = [&](Buffer& instBuf, Buffer& store, VkAccelerationStructureKHR& tl, uint32_t count, uint32_t maxCount, bool fastBuild, Buffer* keepScratch) {
        VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR}; g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR; g.geometry.instances.data.deviceAddress = instBuf.addr;
        VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR}; info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        info.flags = fastBuild ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; info.geometryCount = 1; info.pGeometries = &g;
        VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(A.dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &maxCount, &sz);
        store = makeBuffer(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR}; ci.buffer = store.b; ci.size = sz.accelerationStructureSize; ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        VKC(vkCreateAccelerationStructureKHR(A.dev, &ci, nullptr, &tl));
        Buffer sc = makeBuffer(sz.buildScratchSize + A.scratchAlign, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        info.dstAccelerationStructure = tl; info.scratchData.deviceAddress = alignUp(sc.addr);
        VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0}; const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
        VkCommandBuffer c = beginOnce(); vkCmdBuildAccelerationStructuresKHR(c, 1, &info, &pr); endOnce(c);
        if (keepScratch) *keepScratch = sc; else freeBuffer(sc);
        return sz.accelerationStructureSize;
    };
    const VkBufferUsageFlags instUse = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    A.tlasInst = makeBuffer(inst.size() * sizeof(VkAccelerationStructureInstanceKHR), instUse, true);
    memcpy(A.tlasInst.map, inst.data(), inst.size() * sizeof(VkAccelerationStructureInstanceKHR));
    VkDeviceSize tl = buildTlas(A.tlasInst, A.tlasBuf, A.tlas, (uint32_t)inst.size(), (uint32_t)inst.size(), false, nullptr);
    // ---- Vogel: ein verformbares Netz -> eigenes BLAS (pro Frame neu) + TLAS mit einer Instanz
    A.birdVerts = (uint32_t)W.birdRest.size(); A.birdTris = (uint32_t)W.birdIdx.size() / 3;
    const VkBufferUsageFlags birdUse = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    A.birdVB = makeBuffer(A.birdVerts * sizeof(Vtx), birdUse, true); A.birdIB = makeBuffer(W.birdIdx.size() * 4, birdUse, true);
    memcpy(A.birdVB.map, W.birdRest.data(), A.birdVerts * sizeof(Vtx)); memcpy(A.birdIB.map, W.birdIdx.data(), W.birdIdx.size() * 4);
    {
        VkAccelerationStructureGeometryKHR g; VkAccelerationStructureBuildGeometryInfoKHR info; birdBlasInfo(g, info);
        VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(A.dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &A.birdTris, &sz);
        A.birdBlasBuf = makeBuffer(sz.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR}; ci.buffer = A.birdBlasBuf.b; ci.size = sz.accelerationStructureSize; ci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VKC(vkCreateAccelerationStructureKHR(A.dev, &ci, nullptr, &A.birdBlas));
        VkAccelerationStructureDeviceAddressInfoKHR di{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR}; di.accelerationStructure = A.birdBlas; A.birdBlasAddr = vkGetAccelerationStructureDeviceAddressKHR(A.dev, &di);
        A.birdBlasScratch = makeBuffer(sz.buildScratchSize + A.scratchAlign, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        info.dstAccelerationStructure = A.birdBlas; info.scratchData.deviceAddress = alignUp(A.birdBlasScratch.addr);
        VkAccelerationStructureBuildRangeInfoKHR range{A.birdTris, 0, 0, 0}; const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
        VkCommandBuffer c = beginOnce(); vkCmdBuildAccelerationStructuresKHR(c, 1, &info, &pr); endOnce(c);
    }
    A.birdInst = makeBuffer(sizeof(VkAccelerationStructureInstanceKHR), instUse, true);
    { VkAccelerationStructureInstanceKHR i{}; memcpy(&i.transform, ident, 48); i.mask = 0xFF; i.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR; i.accelerationStructureReference = A.birdBlasAddr; memcpy(A.birdInst.map, &i, sizeof i); }
    buildTlas(A.birdInst, A.birdBuf, A.birdTlas, 1, 1, true, &A.birdScratch);
    logf("Vogel: %u Dreiecke, ein zusammenhaengendes Netz", A.birdTris);
    logf("Beschleunigungsstrukturen: %.0f MB BLAS, %.1f MB TLAS (%zu Instanzen)", asTotal / 1048576.0, tl / 1048576.0, inst.size());
    W.insts.clear(); W.insts.shrink_to_fit();
}

static void recordBird(VkCommandBuffer c, const xform& B) {
    const VkDeviceSize al = A.scratchAlign;
    {   // BLAS aus dem soeben verformten Netz
        VkAccelerationStructureGeometryKHR g; VkAccelerationStructureBuildGeometryInfoKHR info; birdBlasInfo(g, info);
        info.dstAccelerationStructure = A.birdBlas; info.scratchData.deviceAddress = (A.birdBlasScratch.addr + al - 1) / al * al;
        VkAccelerationStructureBuildRangeInfoKHR range{A.birdTris, 0, 0, 0}; const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
        vkCmdBuildAccelerationStructuresKHR(c, 1, &info, &pr);
        memBarrier(c, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
    }
    VkAccelerationStructureInstanceKHR i{}; float m[12]; B.toRows(m); memcpy(&i.transform, m, 48);
    i.instanceCustomIndex = 0; i.mask = 0xFF; i.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR; i.accelerationStructureReference = A.birdBlasAddr;
    memcpy(A.birdInst.map, &i, sizeof i);
    VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR}; g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR; g.geometry.instances.data.deviceAddress = A.birdInst.addr;
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR}; info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR; info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; info.geometryCount = 1; info.pGeometries = &g;
    info.dstAccelerationStructure = A.birdTlas; info.scratchData.deviceAddress = (A.birdScratch.addr + al - 1) / al * al;
    VkAccelerationStructureBuildRangeInfoKHR range{1, 0, 0, 0}; const VkAccelerationStructureBuildRangeInfoKHR* pr = &range;
    vkCmdBuildAccelerationStructuresKHR(c, 1, &info, &pr);
    memBarrier(c, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

// ------------------------------------------------------------------ Pipelines, Renderziele
static void createPipelines() {
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, I = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, B = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    A.pSky = makeCompute(SPV_sky_comp, SPV_sky_comp_size, {U, I});
    A.pRt = makeCompute(SPV_rt_comp, SPV_rt_comp_size, {U, S, S, B, B, T, I, I, I, I, I, I, I, B, B});
    A.pTemporal = makeCompute(SPV_temporal_comp, SPV_temporal_comp_size, {U, T, T, T, T, T, T, T, I, I});
    A.pAtrous = makeCompute(SPV_atrous_comp, SPV_atrous_comp_size, {U, T, T, I}, 4);
    A.pComposite = makeCompute(SPV_composite_comp, SPV_composite_comp_size, {U, T, T, T, T, T, I});
    A.pTaa = makeCompute(SPV_taa_comp, SPV_taa_comp_size, {U, T, T, T, T, I});
    A.pBloom = makeCompute(SPV_bloom_comp, SPV_bloom_comp_size, {U, T, I}, 4);
    A.pExposure = makeCompute(SPV_exposure_comp, SPV_exposure_comp_size, {U, T, I});
    // Abschluss-Pass als Grafik-Pipeline direkt in das Swapchain-Bild
    makeLayout(A.pFinal, {U, T, T, T, T}, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
    VkShaderModule vs = makeShader(SPV_final_vert, SPV_final_vert_size), fs = makeShader(SPV_final_frag, SPV_final_frag_size);
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main"; st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; rci.colorAttachmentCount = 1; rci.pColorAttachmentFormats = &A.scFmt;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; gp.pNext = &rci; gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &ds; gp.layout = A.pFinal.pl;
    VKC(vkCreateGraphicsPipelines(A.dev, VK_NULL_HANDLE, 1, &gp, nullptr, &A.pFinal.pipe));
    vkDestroyShaderModule(A.dev, vs, nullptr); vkDestroyShaderModule(A.dev, fs, nullptr);
}

static const uint32_t SKY_W = 384, SKY_H = 192, HUD_W = 1024, HUD_H = 400;
static void createTargets() {
    vkDeviceWaitIdle(A.dev);
    Image* all[] = {&A.albedo, &A.nd[0], &A.nd[1], &A.direct, &A.indirect, &A.spec, &A.fogI, &A.motion, &A.histD[0], &A.histD[1], &A.histI[0], &A.histI[1], &A.atA, &A.atB, &A.hdr, &A.taa[0], &A.taa[1]};
    for (auto p : all) freeImage(*p);
    freeImage(A.bloomA); freeImage(A.bloomB);
    float sc = QUALITY[A.quality].scale;
    A.rw = std::max(64u, (uint32_t)std::lround(A.scExt.width * sc)); A.rh = std::max(64u, (uint32_t)std::lround(A.scExt.height * sc));
    const VkImageUsageFlags use = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (auto p : all) *p = makeImage(A.rw, A.rh, p == &A.albedo ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT, use);
    A.bloomA = makeImage(std::max(16u, A.rw / 4), std::max(16u, A.rh / 4), VK_FORMAT_R16G16B16A16_SFLOAT, use); A.bloomB = makeImage(A.bloomA.w, A.bloomA.h, VK_FORMAT_R16G16B16A16_SFLOAT, use);
    bool first = !A.skyImg.i;
    if (first) { A.skyImg = makeImage(SKY_W, SKY_H, VK_FORMAT_R16G16B16A16_SFLOAT, use); A.expImg = makeImage(1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, use); A.hud = makeImage(HUD_W, HUD_H, VK_FORMAT_R8G8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        A.hudStage = makeBuffer(HUD_W * HUD_H * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true); memset(A.hudStage.map, 0, HUD_W * HUD_H * 2); }
    VkCommandBuffer c = beginOnce();
    VkClearColorValue zero{}; VkImageSubresourceRange rg{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto init = [&](Image& im) { imgBarrier(c, im.i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT); vkCmdClearColorImage(c, im.i, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rg); };
    for (auto p : all) init(*p);
    init(A.bloomA); init(A.bloomB);
    if (first) { init(A.skyImg); init(A.hud); init(A.expImg); }
    endOnce(c);
    VKC(vkResetDescriptorPool(A.dev, A.dpool, 0));
    VkSampler n = A.sNearest, l = A.sLinear;
    A.sSkySet = makeSet(A.pSky, {dUbo(), dImg(A.skyImg)});
    for (int p = 0; p < 2; p++) {
        A.sRt[p] = makeSet(A.pRt, {dUbo(), dAs(A.tlas), dAs(A.birdTlas), dBuf(A.vb), dBuf(A.ib), dTex(A.skyImg, A.sSky), dImg(A.albedo), dImg(A.nd[p]), dImg(A.direct), dImg(A.indirect), dImg(A.spec), dImg(A.fogI), dImg(A.motion), dBuf(A.birdVB), dBuf(A.birdIB)});
        A.sTemporal[p] = makeSet(A.pTemporal, {dUbo(), dTex(A.direct, n), dTex(A.indirect, n), dTex(A.motion, n), dTex(A.nd[p], n), dTex(A.nd[1 - p], n), dTex(A.histD[1 - p], n), dTex(A.histI[1 - p], n), dImg(A.histD[p]), dImg(A.histI[p])});
        A.sAtrous[p][0] = makeSet(A.pAtrous, {dUbo(), dTex(A.histI[p], n), dTex(A.nd[p], n), dImg(A.atA)});
        A.sAtrous[p][1] = makeSet(A.pAtrous, {dUbo(), dTex(A.atA, n), dTex(A.nd[p], n), dImg(A.atB)});
        A.sAtrous[p][2] = makeSet(A.pAtrous, {dUbo(), dTex(A.atB, n), dTex(A.nd[p], n), dImg(A.atA)});
        A.sComposite[p] = makeSet(A.pComposite, {dUbo(), dTex(A.albedo, n), dTex(A.histD[p], n), dTex(A.atA, n), dTex(A.spec, n), dTex(A.fogI, n), dImg(A.hdr)});
        A.sTaa[p] = makeSet(A.pTaa, {dUbo(), dTex(A.hdr, n), dTex(A.taa[1 - p], l), dTex(A.motion, n), dTex(A.nd[p], n), dImg(A.taa[p])});
        A.sBloom[p][0] = makeSet(A.pBloom, {dUbo(), dTex(A.taa[p], l), dImg(A.bloomA)});
        A.sBloom[p][1] = makeSet(A.pBloom, {dUbo(), dTex(A.bloomA, l), dImg(A.bloomB)});
        A.sBloom[p][2] = makeSet(A.pBloom, {dUbo(), dTex(A.bloomB, l), dImg(A.bloomA)});
        A.sExposure[p] = makeSet(A.pExposure, {dUbo(), dTex(A.taa[p], l), dImg(A.expImg)});
        A.sFinal[p] = makeSet(A.pFinal, {dUbo(), dTex(A.taa[p], l), dTex(A.hud, l), dTex(A.bloomA, l), dTex(A.expImg, n)});
    }
    A.frame = 0; A.havePrevCam = false; A.lastSkySun = vec3(0, 0, 0); A.hudDirty = true;
    logf("Renderaufloesung %ux%u (Fenster %ux%u, Qualitaet %s)", A.rw, A.rh, A.scExt.width, A.scExt.height, QUALITY[A.quality].name);
}

// ------------------------------------------------------------------ Himmel / Tageszeit (CPU-Teil)
struct SkyState { vec3 sunDir, moonDir, sunCol, moonCol, lightDir, lightCol; float lightRad, exposure, night, starVis, fogDens, fogFall; bool lightOn; };
static vec3 transmittance(vec3 d) {
    const double Rp = 6360e3, Ra = 6420e3; double oy = Rp + 150.0;
    double b = oy * d.y, c = oy * oy - Ra * Ra, disc = b * b - c; if (disc < 0) return vec3(0);
    double t1 = -b + std::sqrt(disc);
    double cp = oy * oy - Rp * Rp, dp = b * b - cp; if (dp > 0 && (-b - std::sqrt(dp)) > 0) return vec3(0);
    const int N = 48; double oR = 0, oM = 0, oO = 0, ds = t1 / N;
    for (int i = 0; i < N; i++) { double t = (i + 0.5) * ds; double px = d.x * t, py = oy + d.y * t, pz = d.z * t; double h = std::sqrt(px * px + py * py + pz * pz) - Rp;
        oR += std::exp(-h / 8000.0) * ds; oM += std::exp(-h / 1200.0) * ds; oO += std::max(0.0, 1.0 - std::fabs(h - 25000.0) / 15000.0) * ds; }
    const double bR[3] = {5.8e-6, 13.5e-6, 33.1e-6}, bO[3] = {0.65e-6, 1.881e-6, 0.085e-6}; const double bM = 21e-6 * 1.1;
    return vec3((float)std::exp(-(bR[0] * oR + bM * oM + bO[0] * oO)), (float)std::exp(-(bR[1] * oR + bM * oM + bO[1] * oO)), (float)std::exp(-(bR[2] * oR + bM * oM + bO[2] * oO)));
}
static float lum(vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
static SkyState computeSky(float hour) {
    SkyState s;
    const float lat = 48.7f * PI / 180;
    auto dirOf = [&](float H, float dec) { float e = -std::cos(dec) * std::sin(H), u = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(H), n = std::cos(lat) * std::sin(dec) - std::sin(lat) * std::cos(dec) * std::cos(H); return normalize(vec3(e, u, -n)); };
    float H = (hour - 12.f) / 24.f * 2 * PI;
    s.sunDir = dirOf(H, 17.f * PI / 180); s.moonDir = dirOf(H + PI + 0.45f, -9.f * PI / 180);
    const float Isun = 20.f, Imoon = 0.05f;
    vec3 Ts = transmittance(normalize(vec3(s.sunDir.x, std::max(s.sunDir.y, 0.002f), s.sunDir.z))) * smoothstepf(-0.035f, 0.012f, s.sunDir.y);
    vec3 Tm = transmittance(normalize(vec3(s.moonDir.x, std::max(s.moonDir.y, 0.002f), s.moonDir.z))) * smoothstepf(-0.03f, 0.03f, s.moonDir.y);
    s.sunCol = Ts * (Isun / PI); s.moonCol = Tm * vec3(0.8f, 0.9f, 1.0f) * (Imoon / PI);
    bool sun = lum(s.sunCol) >= lum(s.moonCol);
    s.lightDir = sun ? s.sunDir : s.moonDir; s.lightCol = sun ? s.sunCol : s.moonCol; s.lightRad = sun ? 0.008f : 0.012f;
    s.lightOn = lum(s.lightCol) > 2e-5f && s.lightDir.y > 0.0f;
    float elev = std::asin(clampf(s.sunDir.y, -1, 1)) * 180 / PI;
    s.night = smoothstepf(-2.f, -12.f, elev);
    s.starVis = smoothstepf(-5.f, -14.f, elev) * 0.045f;
    float k = smoothstepf(5.f, -13.f, elev);
    s.exposure = std::exp(mixf(std::log(0.27f), std::log(40.f), k));
    float mist = std::exp(-std::pow((hour - 6.3f) / 1.5f, 2.f));
    s.fogDens = 0.00026f + 0.0011f * mist + 0.00025f * s.night; s.fogFall = mixf(1.f / 150.f, 1.f / 40.f, mist);
    return s;
}

// ------------------------------------------------------------------ HUD
static void hudClear() { memset(A.hudStage.map, 0, HUD_W * HUD_H * 2); }
static void hudPanel(int x0, int y0, int x1, int y1) {
    uint8_t* p = (uint8_t*)A.hudStage.map;
    for (int y = std::max(0, y0); y < std::min((int)HUD_H, y1); y++) for (int x = std::max(0, x0); x < std::min((int)HUD_W, x1); x++) {
        int dx = std::min(x - x0, x1 - 1 - x), dy = std::min(y - y0, y1 - 1 - y); if (dx < 3 && dy < 3 && dx + dy < 3) continue; // abgerundete Ecken
        p[(y * HUD_W + x) * 2 + 1] = 255; }
}
static int hudText(int x, int y, const char* s) {
    uint8_t* p = (uint8_t*)A.hudStage.map; int cx = x;
    for (const unsigned char* c = (const unsigned char*)s; *c; c++) {
        int g = -1;
        if (*c == '\n') { cx = x; y += FONT_CH + 2; continue; }
        if (*c >= 32 && *c < 127) g = *c - 32;
        else if (*c == 0xC3 && c[1]) { c++; switch (*c) { case 0xA4: g = 95; break; case 0xB6: g = 96; break; case 0xBC: g = 97; break; case 0x84: g = 98; break; case 0x96: g = 99; break; case 0x9C: g = 100; break; case 0x9F: g = 101; break; } }
        else if (*c == 0xC2 && c[1]) { c++; if (*c == 0xB0) g = 102; }
        if (g >= 0 && g < FONT_N) for (int j = 0; j < FONT_CH; j++) for (int i = 0; i < FONT_CW; i++) { int px = cx + i, py = y + j; if (px < 0 || py < 0 || px >= (int)HUD_W || py >= (int)HUD_H) continue;
            uint8_t v = FONT_DATA[g][j * FONT_CW + i]; uint8_t& d = p[(py * HUD_W + px) * 2]; if (v > d) d = v; }
        cx += FONT_CW;
    }
    return y + FONT_CH + 2;
}
static void updateHud() {
    hudClear();
    char buf[512]; int hh = int(A.hour), mm = int((A.hour - hh) * 60);
    char tinfo[96]; if (A.timeRun) snprintf(tinfo, sizeof tinfo, "Zeit läuft (%.3g h/min)", A.timeRate); else snprintf(tinfo, sizeof tinfo, "Zeit angehalten");
    hudPanel(10, 10, 700, 10 + 2 * (FONT_CH + 2) + 12);
    snprintf(buf, sizeof buf, "Uhrzeit %02d:%02d   %s   [H] Hilfe", hh, mm, tinfo); hudText(20, 16, buf);
    snprintf(buf, sizeof buf, "Tempo %3.0f km/h   Höhe %3.0f m   %3.0f FPS   %s %ux%u", A.speed * 3.6f, A.pos.y, A.fps, QUALITY[A.quality].name, A.rw, A.rh); hudText(20, 16 + FONT_CH + 2, buf);
    int y = 80;
    if (A.paused) { hudPanel(10, y, 700, y + FONT_CH + 14); hudText(20, y + 6, "PAUSE   [Esc] weiter   [X] Spiel beenden"); y += FONT_CH + 24; }
    if (A.help) {
        const char* h =
            "W / S        schneller / langsamer\n"
            "A / D        nach links / rechts drehen\n"
            "Maus         Richtung, steigen und sinken\n"
            "Q / E, Rad   Tageszeit zurück / vor\n"
            "1 - 6        Morgen, Vormittag, Mittag, Abend, Dämmerung, Nacht\n"
            "T, + / -     Zeitablauf an/aus, schneller / langsamer\n"
            "N / M        weniger / mehr Wolken\n"
            "C            Kamera wechseln        R  zum Start\n"
            "F1 - F4      Qualität: Leistung, Ausgewogen, Hoch, Ultra\n"
            "F11, V, Tab  Vollbild, VSync, Anzeige ausblenden\n"
            "Esc          Pause";
        hudPanel(10, y, 700, y + 11 * (FONT_CH + 2) + 12); hudText(20, y + 6, h);
    }
    A.hudDirty = true;
}

// ------------------------------------------------------------------ Eingabe + Spiel
static bool key(int k) { return glfwGetKey(A.win, k) == GLFW_PRESS; }
static bool pressed(int k) { bool d = key(k), r = d && !A.keyPrev[k]; A.keyPrev[k] = d; return r; }
static void setFullscreen(bool fs) {
    GLFWmonitor* mon = glfwGetPrimaryMonitor(); const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    if (fs && vm) { glfwGetWindowPos(A.win, &A.winX, &A.winY); glfwGetWindowSize(A.win, &A.winW, &A.winH); glfwSetWindowMonitor(A.win, mon, 0, 0, vm->width, vm->height, vm->refreshRate); }
    else glfwSetWindowMonitor(A.win, nullptr, A.winX, A.winY, A.winW, A.winH, 0);
    A.fullscreen = fs; A.swapDirty = true;
}
static void resetBird() { A.pos = A.world.spawn; A.yaw = A.camYaw = PI; A.pitch = A.camPitch = 0; A.roll = 0; A.speed = 14; A.turn = 0; }
static vec3 fwdOf(float yaw, float pitch) { return vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)); }
static float angDiff(float a, float b) { float d = std::fmod(a - b + PI, 2 * PI); if (d < 0) d += 2 * PI; return d - PI; }

static void handleKeys() {
    if (pressed(GLFW_KEY_ESCAPE)) { A.paused = !A.paused; glfwSetInputMode(A.win, GLFW_CURSOR, A.paused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED); A.haveMouse = false; updateHud(); }
    if (A.paused && pressed(GLFW_KEY_X)) glfwSetWindowShouldClose(A.win, 1);
    if (pressed(GLFW_KEY_H)) { A.help = !A.help; A.helpUntil = 0; updateHud(); }
    if (pressed(GLFW_KEY_TAB)) A.hudOn = !A.hudOn;
    if (pressed(GLFW_KEY_C)) A.camMode = (A.camMode + 1) % 3;
    if (pressed(GLFW_KEY_R)) resetBird();
    if (pressed(GLFW_KEY_T)) { A.timeRun = !A.timeRun; updateHud(); }
    if (pressed(GLFW_KEY_V)) { A.opt.vsync = !A.opt.vsync; A.swapDirty = true; }
    if (pressed(GLFW_KEY_F11)) setFullscreen(!A.fullscreen);
    if (pressed(GLFW_KEY_KP_ADD) || pressed(GLFW_KEY_EQUAL) || pressed(GLFW_KEY_RIGHT_BRACKET) || pressed(GLFW_KEY_PAGE_UP)) { A.timeRate = std::min(A.timeRate * 2, 64.f); A.timeRun = true; updateHud(); }
    if (pressed(GLFW_KEY_KP_SUBTRACT) || pressed(GLFW_KEY_MINUS) || pressed(GLFW_KEY_SLASH) || pressed(GLFW_KEY_PAGE_DOWN)) { A.timeRate = std::max(A.timeRate / 2, 0.125f); updateHud(); }
    const float presets[6] = {5.6f, 9.5f, 13.0f, 18.3f, 19.75f, 0.5f};
    for (int i = 0; i < 6; i++) if (pressed(GLFW_KEY_1 + i)) { A.hour = presets[i]; updateHud(); }
    for (int i = 0; i < 4; i++) if (pressed(GLFW_KEY_F1 + i) && A.quality != i) { A.quality = i; A.targetsDirty = true; }
}

static void updateGame(float dt) {
    // Tageszeit
    A.timeScrub = false;
    if (!A.paused) {
        float scrub = (key(GLFW_KEY_E) ? 1.f : 0.f) - (key(GLFW_KEY_Q) ? 1.f : 0.f);
        if (scrub != 0) { A.hour += scrub * dt * 2.2f; A.timeScrub = true; }
        if (A.scrollAcc != 0) { A.hour += (float)A.scrollAcc * 0.25f; A.scrollAcc = 0; A.timeScrub = true; }
        if (A.timeRun) A.hour += dt * A.timeRate / 60.f;
        A.hour = std::fmod(A.hour + 24.f, 24.f);
        A.cloudCov = clampf(A.cloudCov + ((key(GLFW_KEY_M) ? 1.f : 0.f) - (key(GLFW_KEY_N) ? 1.f : 0.f)) * dt * 0.35f, 0.f, 1.f);
        A.cloudTime += dt * (1.f + 40.f * std::fabs(A.timeScrub ? 0.f : 0.f));
        A.gameTime += dt;
    }
    A.prevBirdPos = A.pos;
    if (A.paused) { A.mdx = A.mdy = 0; return; }
    const float sens = 0.0021f;
    float myaw = -(float)A.mdx * sens, mpitch = -(float)A.mdy * sens; A.mdx = A.mdy = 0;
    float turnIn = (key(GLFW_KEY_A) ? 1.f : 0.f) - (key(GLFW_KEY_D) ? 1.f : 0.f);
    A.turn = mixf(A.turn, turnIn * 1.15f, 1 - std::exp(-dt * 5));
    float dyaw = A.turn * dt + myaw;
    A.yaw += dyaw; A.pitch = clampf(A.pitch + mpitch, -1.35f, 1.35f);
    A.yawRate = mixf(A.yawRate, dt > 0 ? dyaw / dt : 0.f, 1 - std::exp(-dt * 8));
    bool kw = key(GLFW_KEY_W), ks = key(GLFW_KEY_S);
    if (A.opt.shotFrames && !A.opt.move) { /* Standbild fuer Screenshots */ }
    else {
        A.speed += ((kw ? 13.f : 0.f) - (ks ? 13.f : 0.f)) * dt - std::sin(A.pitch) * 7.f * dt;
        if (!kw && !ks) A.speed += (15.f - A.speed) * 0.06f * dt;
        A.speed = clampf(A.speed, 4.5f, 52.f);
        vec3 f = fwdOf(A.yaw, A.pitch);
        A.pos += f * (A.speed * dt);
        // Weltgrenze: sanft zurueck zur Stadt lenken
        float r = std::sqrt(A.pos.x * A.pos.x + A.pos.z * A.pos.z);
        float edge = smoothstepf(1350.f, 1550.f, r);
        if (edge > 0) { float want = std::atan2(-A.pos.x, -A.pos.z); A.yaw += clampf(angDiff(want, A.yaw), -1.f, 1.f) * dt * 1.6f * edge; }
        if (A.pos.y > 480) A.pitch -= dt * 0.8f * smoothstepf(480, 560, A.pos.y);
        vec3 n;
        if (A.world.collide(A.pos, 0.5f, n)) {
            float dn = dot(f, n);
            if (dn < 0) { vec3 f2 = normalize(f - n * (dn * 1.08f)); A.yaw = std::atan2(f2.x, f2.z); A.pitch = clampf(std::asin(clampf(f2.y, -1, 1)), -1.35f, 1.35f); A.speed = std::max(A.speed * (1 - 0.5f * std::min(-dn, 1.f)), 5.f); }
        }
    }
    A.roll = mixf(A.roll, clampf(-A.yawRate * 0.55f, -1.1f, 1.1f), 1 - std::exp(-dt * 5));
    float wantFlap = (kw || A.pitch > 0.18f || A.speed < 9.f) ? 1.f : 0.1f;
    A.flapAmt = mixf(A.flapAmt, wantFlap, 1 - std::exp(-dt * 2.5f));
    A.flap += dt * 2 * PI * (2.3f + 1.3f * A.flapAmt);
    if (A.opt.flapPhase >= 0) { A.flap = A.opt.flapPhase; A.flapAmt = 1; }
    // Kamera
    A.camYaw += angDiff(A.yaw, A.camYaw) * (1 - std::exp(-dt * 6));
    A.camPitch = mixf(A.camPitch, A.pitch, 1 - std::exp(-dt * 5));
}

static void computeCamera(float aspect) {
    vec3 f = fwdOf(A.yaw, A.pitch);
    vec3 cf = fwdOf(A.camYaw + A.opt.orbitDeg * PI / 180, A.camPitch * 0.85f + A.opt.orbitPitch * PI / 180);
    vec3 target, cp;
    if (A.camMode == 2) { vec3 up(0, 1, 0); cp = A.pos + f * 0.42f + up * 0.2f; target = cp + f; }
    else {
        float dist = A.camMode == 0 ? 3.6f : 8.5f, upo = A.camMode == 0 ? 1.0f : 2.4f;
        float t = 1.f;
        for (; t > 0.2f; t -= 0.08f) { vec3 c = A.pos - cf * (dist * t) + vec3(0, upo * t, 0); float gy = std::max(A.world.height(c.x, c.z), A.world.waterLevel) + 0.4f; if (c.y > gy && !A.world.insideBox(c, 0.35f)) break; }
        A.camDistF = std::min(t, mixf(A.camDistF, t, 0.08f));
        cp = A.pos - cf * (dist * A.camDistF) + vec3(0, upo * A.camDistF, 0);
        target = A.pos + f * 1.6f + vec3(0, 0.45f, 0);
    }
    vec3 cdir = normalize(target - cp);
    vec3 right = normalize(cross(cdir, vec3(0, 1, 0))), up = cross(right, cdir);
    float fovY = (57.f + clampf((A.speed - 14.f) / 38.f, 0.f, 1.f) * 12.f) * PI / 180;
    A.tanY = std::tan(fovY / 2); A.tanX = A.tanY * aspect;
    A.camPos = cp; A.camF = cdir; A.camR = right; A.camU = up;
}

static float halton(uint32_t i, uint32_t b) { float f = 1, r = 0; while (i) { f /= b; r += f * (i % b); i /= b; } return r; }

// ------------------------------------------------------------------ Frame
static void writeShot(const char* path, uint32_t w, uint32_t h, bool bgr) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h); const uint8_t* p = (const uint8_t*)A.shotBuf.map;
    for (uint32_t i = 0; i < w * h; i++) { uint8_t px[3] = {p[i * 4 + (bgr ? 2 : 0)], p[i * 4 + 1], p[i * 4 + (bgr ? 0 : 2)]}; fwrite(px, 1, 3, f); }
    fclose(f);
}

static void drawFrame(float dt, bool takeShot) {
    VKC(vkWaitForFences(A.dev, 1, &A.fence, VK_TRUE, UINT64_MAX));
    if (A.swapDirty || A.targetsDirty) {
        vkDeviceWaitIdle(A.dev);
        if (A.swapDirty) { if (!createSwapchain()) return; }
        A.swapDirty = A.targetsDirty = false; createTargets(); updateHud();
    }
    uint32_t idx = 0; VkResult ar = vkAcquireNextImageKHR(A.dev, A.sc, UINT64_MAX, A.semAcq, VK_NULL_HANDLE, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) { A.swapDirty = true; return; }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) fatal("Bildausgabe fehlgeschlagen (Code %d).", (int)ar);
    VKC(vkResetFences(A.dev, 1, &A.fence));

    // ---- Uniforms
    computeCamera(float(A.scExt.width) / float(A.scExt.height));
    if (!A.havePrevCam) { A.pCamPos = A.camPos; A.pCamR = A.camR; A.pCamU = A.camU; A.pCamF = A.camF; A.pTanX = A.tanX; A.pTanY = A.tanY; A.prevBirdPos = A.pos; A.havePrevCam = true; }
    SkyState S = computeSky(A.hour);
    const Quality& Q = QUALITY[A.quality];
    FrameUBO u{}; int p = int(A.frame & 1);
    set4(u.camPos, A.camPos, (float)A.gameTime); set4(u.camRight, A.camR * A.tanX); set4(u.camUp, A.camU * A.tanY); set4(u.camFwd, A.camF);
    set4(u.pCamPos, A.pCamPos); set4(u.pCamRight, A.pCamR / A.pTanX); set4(u.pCamUp, A.pCamU / A.pTanY); set4(u.pCamFwd, A.pCamF);
    set4(u.sunDir, S.sunDir); set4(u.sunColor, S.sunCol); set4(u.moonDir, S.moonDir); set4(u.moonColor, S.moonCol);
    set4(u.lightDir, S.lightDir, S.lightRad); set4(u.lightColor, S.lightCol, S.lightOn ? 1.f : 0.f);
    u.sky[0] = 20.f; u.sky[1] = 0.05f; u.sky[2] = S.starVis; u.sky[3] = A.cloudCov;
    u.fog[0] = S.fogDens; u.fog[1] = S.fogFall; u.fog[2] = (float)Q.fogSteps; u.fog[3] = 420.f;
    u.res[0] = (float)A.rw; u.res[1] = (float)A.rh; u.res[2] = 1.f / A.rw; u.res[3] = 1.f / A.rh;
    u.outRes[0] = (float)A.scExt.width; u.outRes[1] = (float)A.scExt.height; u.outRes[2] = 1.f / A.scExt.width; u.outRes[3] = 1.f / A.scExt.height;
    uint32_t ji = uint32_t(A.frame % 16) + 1; u.jitter[0] = halton(ji, 2) - 0.5f; u.jitter[1] = halton(ji, 3) - 0.5f;
    set4(u.birdDelta, A.pos - A.prevBirdPos, S.exposure);
    u.jitter[2] = A.opt.shotFrames ? 0.5f : 1.f - std::exp(-dt * 1.8f);
    u.misc[0] = (float)(A.frame % 1000000); u.misc[1] = mixf(0.20f, 0.05f, S.night); u.misc[2] = (float)Q.gi; u.misc[3] = A.timeScrub ? 6.f : 32.f;
    u.misc2[0] = A.world.waterLevel; u.misc2[1] = std::max(1.f, std::floor(A.scExt.height / 1080.f + 0.35f)); u.misc2[2] = S.night; u.misc2[3] = A.cloudTime;
    u.misc3[0] = A.scSrgb ? 0.f : 1.f; u.misc3[1] = A.hudOn ? 1.f : 0.f; u.misc3[2] = Q.scale < 0.99f ? 0.9f : 0.45f; u.misc3[3] = 1.f;
    memcpy(A.ubo.map, &u, sizeof u);
    A.pCamPos = A.camPos; A.pCamR = A.camR; A.pCamU = A.camU; A.pCamF = A.camF; A.pTanX = A.tanX; A.pTanY = A.tanY;

    // ---- Vogel: das eine Netz fuer den aktuellen Fluegelschlag verformen (CPU), danach BLAS/TLAS neu bauen
    xform B; B.r = rotY(A.yaw) * rotX(-A.pitch) * rotZ(A.roll) * scaleM(1.3f); B.t = A.pos;
    {
        float ph = A.flap, fa = A.flapAmt;
        float shoulder = 0.10f + fa * 0.62f * std::sin(ph) + (1 - fa) * 0.04f * std::sin(ph * 0.21f);
        float wrist = -0.06f + fa * (0.5f * std::sin(ph - 1.0f) - 0.18f);
        float tip = -fa * 0.32f * std::cos(ph - 0.4f) + (1 - fa) * 0.10f;      // Federspitzen biegen sich gegen die Schlagrichtung
        float sweep = 0.08f + 0.14f * (1 - fa) + fa * 0.10f * std::max(0.f, -std::sin(ph - 1.0f)); // im Gleitflug und im Aufschlag leicht angelegt
        A.world.poseBird((Vtx*)A.birdVB.map, shoulder, wrist, tip, sweep);
    }

    VkCommandBuffer c = A.cmd; VKC(vkResetCommandBuffer(c, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; VKC(vkBeginCommandBuffer(c, &bi));
    recordBird(c, B);
    if (A.hudDirty) { VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {HUD_W, HUD_H, 1};
        vkCmdCopyBufferToImage(c, A.hudStage.b, A.hud.i, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
        memBarrier(c, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT); A.hudDirty = false; }
    auto run = [&](const Pass& ps, VkDescriptorSet set, uint32_t w, uint32_t h, int step = -1) {
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, ps.pipe);
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, ps.pl, 0, 1, &set, 0, nullptr);
        if (step >= 0) vkCmdPushConstants(c, ps.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &step);
        vkCmdDispatch(c, (w + 7) / 8, (h + 7) / 8, 1);
        memBarrier(c, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    };
    if (length(S.sunDir - A.lastSkySun) > 1e-5f || A.frame < 2) { run(A.pSky, A.sSkySet, SKY_W, SKY_H); A.lastSkySun = S.sunDir; }
    run(A.pRt, A.sRt[p], A.rw, A.rh);
    run(A.pTemporal, A.sTemporal[p], A.rw, A.rh);
    run(A.pAtrous, A.sAtrous[p][0], A.rw, A.rh, 1);
    run(A.pAtrous, A.sAtrous[p][1], A.rw, A.rh, 2);
    run(A.pAtrous, A.sAtrous[p][2], A.rw, A.rh, 4);
    run(A.pComposite, A.sComposite[p], A.rw, A.rh);
    run(A.pTaa, A.sTaa[p], A.rw, A.rh);
    run(A.pBloom, A.sBloom[p][0], A.bloomA.w, A.bloomA.h, 0);
    run(A.pBloom, A.sBloom[p][1], A.bloomA.w, A.bloomA.h, 1);
    run(A.pBloom, A.sBloom[p][2], A.bloomA.w, A.bloomA.h, 2);
    run(A.pExposure, A.sExposure[p], 1, 1);

    imgBarrier(c, A.scImgs[idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; att.imageView = A.scViews[idx]; att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO}; ri.renderArea = {{0, 0}, A.scExt}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &att;
    vkCmdBeginRendering(c, &ri);
    VkViewport vp{0, 0, (float)A.scExt.width, (float)A.scExt.height, 0, 1}; VkRect2D sr{{0, 0}, A.scExt}; vkCmdSetViewport(c, 0, 1, &vp); vkCmdSetScissor(c, 0, 1, &sr);
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, A.pFinal.pipe);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, A.pFinal.pl, 0, 1, &A.sFinal[p], 0, nullptr);
    vkCmdDraw(c, 3, 1, 0, 0);
    vkCmdEndRendering(c);
    bool shot = takeShot && A.scCanCopy;
    if (shot) {
        if (A.shotBuf.size < VkDeviceSize(A.scExt.width) * A.scExt.height * 4) { freeBuffer(A.shotBuf); A.shotBuf = makeBuffer(VkDeviceSize(A.scExt.width) * A.scExt.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true); }
        imgBarrier(c, A.scImgs[idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {A.scExt.width, A.scExt.height, 1};
        vkCmdCopyImageToBuffer(c, A.scImgs[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, A.shotBuf.b, 1, &r);
        imgBarrier(c, A.scImgs[idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    } else imgBarrier(c, A.scImgs[idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    VKC(vkEndCommandBuffer(c));
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &A.semAcq; si.pWaitDstStageMask = &ws; si.commandBufferCount = 1; si.pCommandBuffers = &c; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &A.semDone;
    VKC(vkQueueSubmit(A.queue, 1, &si, A.fence));
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &A.semDone; pi.swapchainCount = 1; pi.pSwapchains = &A.sc; pi.pImageIndices = &idx;
    VkResult pr = vkQueuePresentKHR(A.queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) A.swapDirty = true;
    else if (pr != VK_SUCCESS) fatal("Bildausgabe fehlgeschlagen (Code %d).", (int)pr);
    if (shot) { VKC(vkWaitForFences(A.dev, 1, &A.fence, VK_TRUE, UINT64_MAX)); writeShot(A.opt.shot.c_str(), A.scExt.width, A.scExt.height, A.scFmt == VK_FORMAT_B8G8R8A8_UNORM || A.scFmt == VK_FORMAT_B8G8R8A8_SRGB); logf("Screenshot: %s", A.opt.shot.c_str()); }
    A.frame++;
}

// ------------------------------------------------------------------ main
static void parseArgs(int argc, char** argv) {
    Options& o = A.opt;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i]; auto nextf = [&]() { return i + 1 < argc ? (float)atof(argv[++i]) : 0.f; };
        if (a == "--windowed" || a == "--fenster") o.windowed = true;
        else if (a == "--validate") o.validate = true;
        else if (a == "--novsync") o.vsync = false;
        else if (a == "--move") o.move = true;
        else if (a == "--nohud") o.nohud = true;
        else if (a == "--size" && i + 2 < argc) { o.width = atoi(argv[++i]); o.height = atoi(argv[++i]); o.windowed = true; }
        else if (a == "--quality") o.quality = (int)nextf();
        else if (a == "--density") o.density = nextf();
        else if (a == "--time") o.hour = nextf();
        else if (a == "--clouds") o.clouds = nextf();
        else if (a == "--cam") o.camMode = (int)nextf();
        else if (a == "--yaw") o.yawDeg = nextf();
        else if (a == "--orbit") o.orbitDeg = nextf();        // nur fuer Testbilder: Kamera um den Vogel schwenken
        else if (a == "--orbitpitch") o.orbitPitch = nextf();
        else if (a == "--flapphase") o.flapPhase = nextf();   // nur fuer Testbilder: Fluegelschlag einfrieren
        else if (a == "--pitch") o.pitchDeg = nextf();
        else if (a == "--pos" && i + 3 < argc) { o.pos.x = (float)atof(argv[++i]); o.pos.y = (float)atof(argv[++i]); o.pos.z = (float)atof(argv[++i]); o.hasPos = true; }
        else if (a == "--shot" && i + 2 < argc) { o.shot = argv[++i]; o.shotFrames = atoi(argv[++i]); o.windowed = true; }
    }
}

int main(int argc, char** argv) {
    gLog = fopen("vogelflug.log", "w");
    parseArgs(argc, argv);
    glfwSetErrorCallback([](int code, const char* d) { logf("GLFW-Fehler %d: %s", code, d); });
    if (!glfwInit()) fatal("Fenstersystem konnte nicht initialisiert werden.");
    if (!glfwVulkanSupported()) fatal("Vulkan wurde nicht gefunden.\nBitte einen aktuellen Grafiktreiber installieren (NVIDIA GeForce-Treiber).");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWmonitor* mon = glfwGetPrimaryMonitor(); const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    bool fs = !A.opt.windowed && vm;
    A.win = fs ? glfwCreateWindow(vm->width, vm->height, "Vogelflug", mon, nullptr) : glfwCreateWindow(A.opt.width, A.opt.height, "Vogelflug", nullptr, nullptr);
    if (!A.win) fatal("Fenster konnte nicht erstellt werden.");
    A.fullscreen = fs; if (fs) { A.winW = std::min(1600, vm->width * 3 / 4); A.winH = std::min(900, vm->height * 3 / 4); }
    initVulkan();
    if (!createSwapchain()) fatal("Swapchain konnte nicht erstellt werden.");
    A.quality = A.opt.quality >= 0 && A.opt.quality < 4 ? A.opt.quality : (A.scExt.height > 1150 ? 1 : 2);
    A.hour = A.opt.hour; A.cloudCov = A.opt.clouds; A.camMode = A.opt.camMode;

    double t0 = glfwGetTime();
    logf("Erzeuge Welt (Dichte %.2f) ...", A.opt.density);
    A.world.generate(A.opt.density, 20261111u);
    logf("Welt erzeugt in %.1f s (Bruecke bei x=%.1f), baue Raytracing-Strukturen ...", glfwGetTime() - t0, A.world.bridgeX);
    buildScene();
    createPipelines();
    createTargets();
    logf("Start nach %.1f s", glfwGetTime() - t0);

    resetBird();
    if (A.opt.hasPos) A.pos = A.opt.pos;
    A.yaw = A.camYaw = A.opt.yawDeg * PI / 180; A.pitch = A.camPitch = A.opt.pitchDeg * PI / 180;
    if (A.opt.shotFrames) { A.timeRun = false; A.help = false; A.hudOn = !A.opt.nohud; }
    glfwSetInputMode(A.win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(A.win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPosCallback(A.win, [](GLFWwindow*, double x, double y) { if (A.haveMouse && !A.paused) { A.mdx += x - A.lastMx; A.mdy += y - A.lastMy; } A.lastMx = x; A.lastMy = y; A.haveMouse = true; });
    glfwSetScrollCallback(A.win, [](GLFWwindow*, double, double y) { A.scrollAcc += y; });
    glfwSetFramebufferSizeCallback(A.win, [](GLFWwindow*, int, int) { A.swapDirty = true; });
    updateHud();

    double last = glfwGetTime(), fpsT = last; int fpsN = 0; uint64_t shotCount = 0;
    while (!glfwWindowShouldClose(A.win)) {
        glfwPollEvents();
        int fw, fh; glfwGetFramebufferSize(A.win, &fw, &fh);
        if (fw == 0 || fh == 0) { glfwWaitEventsTimeout(0.1); last = glfwGetTime(); continue; }
        double now = glfwGetTime(); float dt = (float)std::min(now - last, 0.1); last = now;
        if (A.opt.shotFrames) dt = 1.f / 30.f;
        handleKeys();
        updateGame(dt);
        fpsN++; if (now - fpsT >= 0.5) { A.fps = float(fpsN / (now - fpsT)); fpsN = 0; fpsT = now; }
        if (A.help && A.helpUntil > 0 && A.gameTime > A.helpUntil) { A.help = false; A.helpUntil = 0; }
        if (now >= A.hudNext) { updateHud(); A.hudNext = now + 0.25; }
        bool shot = A.opt.shotFrames && (int)shotCount == A.opt.shotFrames - 1;
        drawFrame(dt, shot);
        if (A.opt.shotFrames && ++shotCount >= (uint64_t)A.opt.shotFrames) break;
    }
    vkDeviceWaitIdle(A.dev);
    // Aufraeumen uebernimmt beim Beenden der Treiber; explizit nur das Noetigste
    glfwDestroyWindow(A.win); glfwTerminate();
    if (gLog) fclose(gLog);
    return 0;
}
