#define STB_IMAGE_IMPLEMENTATION
#include "App.hpp"

#include <etna/Profiling.hpp>
#include <thread>
#include <chrono>

#include <iostream>

static uint64_t g_frameCounter = 0;

App::App()
  : resolution{1280, 720}
  , useVsync{true}
{

  {
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();
    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 2,
    });
  }

  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  {
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    resolution = {w, h};
  }

  commandManager = etna::get_context().createPerFrameCmdMgr();
  oneShotManager = etna::get_context().createOneShotCmdMgr();


  etna::create_program("texture", {INFLIGHT_FRAMES_SHADERS_ROOT  "texture.comp.spv"});
  computePipeline = etna::get_context().getPipelineManager().createComputePipeline("texture", {});
  sampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "computeSampler"});

  buffImage = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "output",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eStorage
      | vk::ImageUsageFlagBits::eTransferSrc
      | vk::ImageUsageFlagBits::eSampled
  });

  etna::create_program(
    "image",
    {INFLIGHT_FRAMES_SHADERS_ROOT  "toy.vert.spv",
     INFLIGHT_FRAMES_SHADERS_ROOT  "toy.frag.spv"}
  );

  graphicsPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "image",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  graphicsSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "graphicsSampler",
  });

  std::cout << "Openning a texture" << std::endl;
  int width, height, channels;
  const auto file = stbi_load(
    INFLIGHT_FRAMES_SHADERS_ROOT  "../../../../resources/textures/texture1.bmp",
    &width,
    &height,
    &channels,
    STBI_rgb_alpha);

  image = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<unsigned int>(width), static_cast<unsigned int>(height), 1},
    .name = "texture",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eStorage
      | vk::ImageUsageFlagBits::eTransferDst
      | vk::ImageUsageFlagBits::eSampled
  });

  etna::BlockingTransferHelper(etna::BlockingTransferHelper::CreateInfo{
                                 .stagingSize = static_cast<std::uint32_t>(width * height),
                               })
    .uploadImage(
      *oneShotManager,
      image,
      0,
      0,
      std::span(reinterpret_cast<const std::byte*>(file), width * height * 4));
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  start = std::chrono::system_clock::now();
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();
    drawFrame();
    g_frameCounter++;
  }
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  ZoneScopedN("drawFrame");
  FrameMark;
  {
    ZoneScopedN("FakeCpuLoad");
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }

  auto currentCmdBuf = commandManager->acquireNext();
  etna::begin_frame();

  auto nextSwapchainImage = vkWindow->acquireNext();
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      {
        ETNA_PROFILE_GPU(currentCmdBuf, "ComputePass");

        etna::set_state(
          currentCmdBuf,
          backbuffer,
          vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferWrite,
          vk::ImageLayout::eTransferDstOptimal,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(currentCmdBuf);

        auto computeTexture = etna::get_shader_program("texture");
        auto computeSet = etna::create_descriptor_set(
          computeTexture.getDescriptorLayoutId(0),
          currentCmdBuf,
          {etna::Binding{0, buffImage.genBinding(sampler.get(), vk::ImageLayout::eGeneral)}});

        const vk::DescriptorSet computeVkSet = computeSet.getVkSet();
        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          computePipeline.getVkPipelineLayout(),
          0,
          1,
          &computeVkSet,
          0,
          nullptr);

        int64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now() - start)
                                .count();
        glm::vec2 mousePosition = osWindow.get()->mouse.freePos;

        pushedParams = {
          .size_x = resolution.x,
          .size_y = resolution.y,
          .time = currentTime / 1000.f,
          .mouse_x = mousePosition.x,
          .mouse_y = mousePosition.y
        };

        currentCmdBuf.pushConstants(
          computePipeline.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eCompute,
          0,
          sizeof(pushedParams),
          &pushedParams);
        etna::flush_barriers(currentCmdBuf);

        currentCmdBuf.dispatch((resolution.x + 31) / 32, (resolution.y + 31) / 32, 1);

        etna::set_state(
          currentCmdBuf,
          buffImage.get(),
          vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferRead,
          vk::ImageLayout::eTransferSrcOptimal,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(currentCmdBuf);
      }

      {
        ETNA_PROFILE_GPU(currentCmdBuf, "GraphicsPass");

        auto imgInfo = etna::get_shader_program("image");
        auto graphicsSet = etna::create_descriptor_set(
          imgInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
            etna::Binding{0, buffImage.genBinding(sampler.get(), vk::ImageLayout::eGeneral)},
            etna::Binding{1, image.genBinding(graphicsSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}
          });
        const vk::DescriptorSet graphicsVkSet = graphicsSet.getVkSet();

        {
          etna::RenderTargetState renderTargets{
            currentCmdBuf,
            {{0, 0}, {resolution.x, resolution.y}},
            {{.image = backbuffer, .view = backbufferView}},
            {}
          };

          currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.getVkPipeline());
          currentCmdBuf.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            graphicsPipeline.getVkPipelineLayout(),
            0,
            1,
            &graphicsVkSet,
            0,
            nullptr);

          currentCmdBuf.pushConstants<vk::DispatchLoaderDynamic>(
            graphicsPipeline.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(pushedParams),
            &pushedParams);

          currentCmdBuf.draw(3, 1, 0, 0);
        }

        etna::flush_barriers(currentCmdBuf);

        etna::set_state(
          currentCmdBuf,
          backbuffer,
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          {},
          vk::ImageLayout::ePresentSrcKHR,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(currentCmdBuf);
      }

    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    auto renderingDone = commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);

    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);
    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
