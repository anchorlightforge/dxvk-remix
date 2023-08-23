/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

// this gets included from other modules, so use full path to external --- ugly!
#include "../../../external/ngx_sdk_dlss/include/nvsdk_ngx.h"
#include <memory>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_semaphore.h"

// run DLFG in graphics queue for debugging
// note that this incurs heavy CPU serialization and is not meant to be used in general
// it also causes waits on unsignaled semaphores for the first N frames (generally OK on Windows, but will cause VL errors)
#define __DLFG_USE_GRAPHICS_QUEUE 0
// Note: Currently Reflex without its Vulkan extension has no way of marking Vulkan queue submits as belonging to a specific frame, rather just using
// which present end markers it is between to associate with a given frame. This causes issues however when we mark the present on the DLFG thread
// as the DLFG thread may be quite a ways disconnected from where rendering work is being submitted such that occasionally 0 or 2 frames worth of
// work will fall in between the present markers here, which causes Reflex to generate long sleeps where it shouldn't, resulting in stutters.
// Additionally, this only really matters for the Present marker right now, the out-of-band Present marker can stay where it should be without causing issues.
// As such, until this Vulkan extension is used in our Reflex implementation the begin/end Presentation calls are moved from the DLFG thread to the submit
// thread as a hack when this workaround is enabled to ensure they are placed in a more suitable location that will always come after render queue submission.
// Do not disable this workaround without good reason to do so (e.g. implementing the Vulkan extension and testing to ensure no stutters exist).
#define __DLFG_REFLEX_WORKAROUND 1

#if __DLFG_USE_GRAPHICS_QUEUE
#define __DLFG_QUEUE graphics
#else
#define __DLFG_QUEUE present
#endif

// Forward declarations from NGX library.
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace dxvk {
  class RtCamera;
  class DxvkDevice;
  class DxvkContext;

  class NGXDLSSContext;
  class NGXDLFGContext;

  class NGXContext {
  public:
    NGXContext(DxvkDevice* device);

    ~NGXContext() {
      shutdown();
    }

    void shutdown();

    bool supportsDLSS() {
      return m_supportsDLSS;
    }

    bool supportsDLFG() {
      return m_supportsDLFG;
    }

    const std::string& getDLFGNotSupportedReason() {
      return m_dlfgNotSupportedReason;
    }
    
    std::unique_ptr<NGXDLSSContext> createDLSSContext();
    std::unique_ptr<NGXDLFGContext> createDLFGContext();

  private:
    bool initialize();

    DxvkDevice* m_device = nullptr;

    bool m_initialized = false;
    bool m_supportsDLSS = false;
    bool m_supportsDLFG = false;

    bool checkDLSSSupport(NVSDK_NGX_Parameter* params);
    bool checkDLFGSupport(NVSDK_NGX_Parameter* params);

    std::string m_dlfgNotSupportedReason;
  };

  class NGXFeatureContext {
  public:
    ~NGXFeatureContext();
    void releaseNGXFeature();

  protected:
    NGXFeatureContext(DxvkDevice* device);

    DxvkDevice* m_device = nullptr;
    NVSDK_NGX_Parameter* m_parameters = nullptr;
    NVSDK_NGX_Handle* m_feature = nullptr;
  };

  class NGXDLSSContext : public NGXFeatureContext {
  public:
    struct OptimalSettings {
      float sharpness;
      uint32_t optimalRenderSize[2];
      uint32_t minRenderSize[2];
      uint32_t maxRenderSize[2];
    };

    // Query optimal DLSS settings for a given resolution and performance/quality profile.
    OptimalSettings queryOptimalSettings(const uint32_t displaySize[2], NVSDK_NGX_PerfQuality_Value perfQuality) const;

    // initialize DLSS context, throws exception on failure
    void initialize(
      Rc<DxvkContext> renderContext,
      uint32_t maxRenderSize[2],
      uint32_t displayOutSize[2],
      //Texture* pTarget,
      bool isContentHDR,
      bool depthInverted,
      bool autoExposure,
      bool sharpening,
      NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf);

    bool evaluate(
      Rc<DxvkContext> renderContext,
      const Resources::Resource* pUnresolvedColor,
      const Resources::Resource* pResolvedColor,
      const Resources::Resource* pMotionVectors,
      const Resources::Resource* pDepth,
      const Resources::Resource* pDiffuseAlbedo,
      const Resources::Resource* pSpecularAlbedo,
      const Resources::Resource* pExposure,
      const Resources::Resource* pPosition,
      const Resources::Resource* pNormals,
      const Resources::Resource* pRoughness,
      const Resources::Resource* pBiasCurrentColorMask,
      bool resetAccumulation,
      bool antiGhost,
      float sharpness,
      float preExposure,
      float jitterOffset[2],
      float motionVectorScale[2],
      bool autoExposure) const;

    // note: ctor is public due to make_unique/unique_ptr, but not intended as public --- use NGXWrapper::createDLSSContext instead
    NGXDLSSContext(DxvkDevice* device);
    ~NGXDLSSContext() = default;
  };

  class NGXDLFGContext : public NGXFeatureContext {
  public:
    typedef void (__cdecl* AppCreateTimelineSyncObjectsCallback_t)(void* app_context,
                                                                   void** pp_sync_obj_signal,
                                                                   uint64_t sync_obj_signal_value,
                                                                   void** pp_sync_obj_wait,
                                                                   uint64_t sync_obj_wait_value);
    typedef void (__cdecl* AppSyncSignalCallback_t)(void* app_context,
                                                    void** pp_cmd_list,
                                                    void* sync_obj_signal,
                                                    uint64_t sync_obj_signal_value);
    typedef void (__cdecl* AppSyncWaitCallback_t)(void* app_context,
                                                  void** pp_cmd_list,
                                                  void* sync_obj_wait,
                                                  uint64_t sync_obj_wait_value,
                                                  int wait_cpu,
                                                  void* sync_obj_signal,
                                                  uint64_t sync_obj_signal_value);
    typedef void (__cdecl* AppSyncFlushCallback_t)(void* app_context,
                                                   void** pp_cmd_list,
                                                   void* sync_obj_signal,
                                                   uint64_t sync_obj_signal_value,
                                                   int wait_cpu);

    typedef enum {
      Failure,
      NeedWaitIdle,
      Success,
    } EvaluateResult;

    void initialize(
      Rc<DxvkContext> renderContext,
VkCommandBuffer commandList,
      uint32_t displayOutSize[2],
      VkFormat outputFormat,
      AppCreateTimelineSyncObjectsCallback_t createTimelineSyncObjectsCallback,
      AppSyncSignalCallback_t syncSignalCallback,
      AppSyncWaitCallback_t syncWaitCallback,
      AppSyncFlushCallback_t syncFlushCallback,
      void* callbackData
      );

    // interpolates one frame
    // DLFG keeps copies of each real frame, so we only need to pass in the current frame here
    // the first kNumWarmUpFrames won't be interpolated so interpolatedOutput may not be valid, this function returns true if interpolation happened
    EvaluateResult evaluate(
      Rc<DxvkContext> renderContext,
VkCommandBuffer clientCommandList,
      Rc<DxvkImageView> interpolatedOutput,
      Rc<DxvkImageView> compositedColorBuffer,
      Rc<DxvkImageView> motionVectors,
      Rc<DxvkImageView> depth,
      const RtCamera& camera, Vector2 motionVectorScale, bool resetHistory);

    // note: ctor is public due to make_unique/unique_ptr, but not intended as public --- use NGXWrapper::createDLFGContext instead
    NGXDLFGContext(DxvkDevice* device);
    ~NGXDLFGContext();

  private:
    VkCommandPool m_ngxInternalCommandPool = nullptr;
  };
}
