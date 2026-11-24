// AMD FidelityFX Super Resolution 2.2.1 (MIT), built from source into the Switch 2 .nro (switch/CMakeLists.txt, pt_fsr2): the scene renders
// at the dynamic resolution's size and FSR 2 reconstructs the screen's size (1080p at most) from it, temporally. The desktop build's FSR
// goes through AMD's prebuilt Windows DLL instead (fsr_backend.cpp); the Switch has no DLL and no NVIDIA driver, so this is the console's
// temporal upscaler.
#include "engine/render/upscale/upscale.h"

#if defined(PT_WITH_FSR2)

#include <ffx_fsr2.h>
#include <vk/ffx_fsr2_vk.h>

#include <cfloat>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/core/log.h"

namespace pt {
namespace {

void Message(FfxFsr2MsgType type, const wchar_t* message) {
    std::string text;
    for (; message && *message; ++message) text.push_back(*message < 128 ? static_cast<char>(*message) : '?');
    if (type == FFX_FSR2_MESSAGE_TYPE_ERROR) {
        LogError("fsr2: {}", text);
    } else {
        LogWarn("fsr2: {}", text);
    }
}

// The context flags the Switch 2's shader permutations were built for (tools/fsr2_vk_shaders.py compiles only these): render-size motion
// vectors without the jitter, reversed-Z depth with an infinite far plane, and HDR colour or not (FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE,
// from UpscaleCreate::hdr). Dynamic resolution: one context for every render size up to the output size.
constexpr uint32_t kContextFlags = FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_DEPTH_INFINITE | FFX_FSR2_ENABLE_DYNAMIC_RESOLUTION;

class Fsr2Backend final : public UpscaleBackend {
public:
    explicit Fsr2Backend(vk::Context& ctx) : ctx_(ctx) {}
    ~Fsr2Backend() override { Release(); }

    UpscalerKind Kind() const override { return UpscalerKind::Fsr; }
    bool Available(std::string& reason) override {
        reason.clear();
        return true;
    }
    bool DynamicResolution() const override { return true; }

    bool Create(VkCommandBuffer, const UpscaleCreate& create) override {
        Release();
        scratch_.assign(ffxFsr2GetScratchMemorySizeVK(ctx_.physical), 0);
        FfxFsr2ContextDescription desc{};
        FfxErrorCode result = ffxFsr2GetInterfaceVK(&desc.callbacks, scratch_.data(), scratch_.size(), ctx_.physical, vkGetDeviceProcAddr);
        if (result != FFX_OK) {
            LogError("fsr2: ffxFsr2GetInterfaceVK failed ({})", result);
            return false;
        }
        desc.device = ffxGetDeviceVK(ctx_.device);
        desc.flags = kContextFlags | (create.hdr ? FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE : 0u);
        if (std::getenv("PT_UPSCALE_CHECK")) {
            desc.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
        }
        // any render size up to the output's: the dynamic resolution changes it without a new context
        desc.maxRenderSize = {create.display.width, create.display.height};
        desc.displaySize = {create.display.width, create.display.height};
        desc.fpMessage = Message;
        result = ffxFsr2ContextCreate(&context_, &desc);
        if (result != FFX_OK) {
            LogError("fsr2: ffxFsr2ContextCreate failed ({}) for {}x{}", result, create.display.width, create.display.height);
            return false;
        }
        created_ = true;
        display_ = create.display;
        LogInfo("fsr2: FSR 2.2.1 created for output {}x{} (render sizes up to it), {} input", create.display.width, create.display.height,
                create.hdr ? "HDR" : "tonemapped (after the post-processing)");
        return true;
    }

    bool Dispatch(const UpscaleDispatch& d) override {
        if (!created_) {
            return false;
        }
        FfxFsr2DispatchDescription u{};
        u.commandList = ffxGetCommandListVK(d.cmd);
        u.color = Resource(d.color, L"FSR2_InputColor", FFX_RESOURCE_STATE_COMPUTE_READ);
        u.depth = Resource(d.depth, L"FSR2_InputDepth", FFX_RESOURCE_STATE_COMPUTE_READ);
        u.motionVectors = Resource(d.motion, L"FSR2_InputMotionVectors", FFX_RESOURCE_STATE_COMPUTE_READ);
        u.exposure = Resource(d.exposure, L"FSR2_InputExposure", FFX_RESOURCE_STATE_COMPUTE_READ);
        u.reactive = Resource(d.reactive, L"FSR2_InputReactiveMap", FFX_RESOURCE_STATE_COMPUTE_READ);
        u.transparencyAndComposition = Resource(d.transparency, L"FSR2_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_COMPUTE_READ);
        u.output = Resource(d.output, L"FSR2_OutputUpscaledColor", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        u.jitterOffset = {d.jitter.x, d.jitter.y};
        u.motionVectorScale = {d.motion_scale.x, d.motion_scale.y};
        u.renderSize = {d.render.width, d.render.height};
        u.enableSharpening = d.sharpness > 0.0f;
        u.sharpness = d.sharpness;
        u.frameTimeDelta = d.frame_ms;
        u.preExposure = d.pre_exposure;
        u.reset = d.reset;
        u.cameraNear = FLT_MAX;  // reversed-Z with an infinite far plane (as the desktop FSR backend passes it)
        u.cameraFar = d.near_plane;
        u.cameraFovAngleVertical = d.fov_y;
        u.viewSpaceToMetersFactor = 1.0f;
        const FfxErrorCode result = ffxFsr2ContextDispatch(&context_, &u);
        if (result != FFX_OK) {
            if (!dispatch_error_logged_) {
                LogError("fsr2: ffxFsr2ContextDispatch failed ({}) at {}x{} -> {}x{}", result, d.render.width, d.render.height, display_.width,
                         display_.height);
                dispatch_error_logged_ = true;
            }
            return false;
        }
        return true;
    }

    void Release() override {
        if (created_) {
            vkDeviceWaitIdle(ctx_.device);
            ffxFsr2ContextDestroy(&context_);
            created_ = false;
        }
    }

private:
    FfxResource Resource(const UpscaleImage& image, const wchar_t* name, FfxResourceStates state) {
        if (!image.Valid()) {
            return ffxGetTextureResourceVK(&context_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, 1, VK_FORMAT_UNDEFINED, name, state);
        }
        return ffxGetTextureResourceVK(&context_, image.image, image.view, image.extent.width, image.extent.height, image.format, name, state);
    }

    vk::Context& ctx_;
    std::vector<unsigned char> scratch_;
    FfxFsr2Context context_{};
    bool created_ = false;
    VkExtent2D display_{};
    bool dispatch_error_logged_ = false;
};

}

std::unique_ptr<UpscaleBackend> CreateFsrBackend(vk::Context& ctx, int generation) {
    if (generation != 3) {
        return nullptr;  // FSR 4 exists for DirectX 12 only
    }
    return std::make_unique<Fsr2Backend>(ctx);
}

}

#endif
