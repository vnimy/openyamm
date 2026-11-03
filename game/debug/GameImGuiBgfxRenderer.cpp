#include "game/debug/GameImGuiBgfxRenderer.h"

#include "game/render/TextureFiltering.h"
#include "game/render/WaterMath.h"

#include <imgui.h>
#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"

namespace OpenYAMM::Game
{
namespace
{
constexpr bgfx::ViewId ImGuiViewId = DebugImGuiView;

static const std::array<bgfx::EmbeddedShader, 3> EmbeddedShaders = {{
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
}};
}

bgfx::VertexLayout GameImGuiBgfxRenderer::createVertexLayout()
{
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return layout;
}

bool GameImGuiBgfxRenderer::initialize()
{
    if (m_initialized)
    {
        return true;
    }

    m_vertexLayout = createVertexLayout();

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const bgfx::ShaderHandle vertexShaderHandle =
        bgfx::createEmbeddedShader(EmbeddedShaders.data(), rendererType, "vs_ocornut_imgui");
    const bgfx::ShaderHandle fragmentShaderHandle =
        bgfx::createEmbeddedShader(EmbeddedShaders.data(), rendererType, "fs_ocornut_imgui");

    if (!bgfx::isValid(vertexShaderHandle) || !bgfx::isValid(fragmentShaderHandle))
    {
        if (bgfx::isValid(vertexShaderHandle))
        {
            bgfx::destroy(vertexShaderHandle);
        }

        if (bgfx::isValid(fragmentShaderHandle))
        {
            bgfx::destroy(fragmentShaderHandle);
        }

        return false;
    }

    m_programHandle = bgfx::createProgram(vertexShaderHandle, fragmentShaderHandle, true);
    m_textureSamplerHandle = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    if (!bgfx::isValid(m_programHandle) || !bgfx::isValid(m_textureSamplerHandle))
    {
        shutdown();
        return false;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = "openyamm_game_imgui_bgfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    // The font atlas grows while the game runs: ImGui 1.92 rasterizes a glyph the first time a size needs it
    // and hands the block over through ImTextureData. Claiming this flag is what keeps that path open.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    m_initialized = true;
    return true;
}

void GameImGuiBgfxRenderer::shutdown()
{
    if (bgfx::isValid(m_textureSamplerHandle))
    {
        bgfx::destroy(m_textureSamplerHandle);
        m_textureSamplerHandle = BGFX_INVALID_HANDLE;
    }

    if (bgfx::isValid(m_programHandle))
    {
        bgfx::destroy(m_programHandle);
        m_programHandle = BGFX_INVALID_HANDLE;
    }

    m_initialized = false;
}

void GameImGuiBgfxRenderer::newFrame()
{
}

namespace
{
bgfx::TextureHandle textureHandleFromId(ImTextureID textureId)
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    handle.idx = static_cast<uint16_t>(static_cast<uintptr_t>(textureId) - 1);
    return handle;
}
}

void GameImGuiBgfxRenderer::updateTextures()
{
    if (!m_initialized)
    {
        return;
    }

    for (ImTextureData *pTexture : ImGui::GetPlatformIO().Textures)
    {
        if (pTexture == nullptr)
        {
            continue;
        }

        if (pTexture->Status == ImTextureStatus_WantCreate)
        {
            const bgfx::TextureFormat::Enum format = pTexture->Format == ImTextureFormat_Alpha8
                ? bgfx::TextureFormat::A8
                : bgraTextureUploadFormat();
            const bgfx::Memory *pMemory = bgfx::copy(
                pTexture->Pixels, static_cast<uint32_t>(pTexture->GetSizeInBytes()));
            const bgfx::TextureHandle handle = bgfx::createTexture2D(
                static_cast<uint16_t>(pTexture->Width),
                static_cast<uint16_t>(pTexture->Height),
                false,
                1,
                format,
                0,
                pMemory);

            if (!bgfx::isValid(handle))
            {
                continue;
            }

            pTexture->SetTexID(static_cast<ImTextureID>(handle.idx + 1));
            pTexture->SetStatus(ImTextureStatus_OK);
        }
        else if (pTexture->Status == ImTextureStatus_WantUpdates)
        {
            const bgfx::TextureHandle handle = textureHandleFromId(pTexture->GetTexID());

            if (!bgfx::isValid(handle))
            {
                pTexture->SetStatus(ImTextureStatus_WantCreate);
                continue;
            }

            for (const ImTextureRect &rect : pTexture->Updates)
            {
                const unsigned char *pSource =
                    pTexture->Pixels + (rect.x + size_t(rect.y) * size_t(pTexture->Width)) * size_t(pTexture->BytesPerPixel);
                bgfx::updateTexture2D(handle, 0, 0, rect.x, rect.y, rect.w, rect.h,
                    bgfx::copy(pSource, static_cast<uint32_t>(size_t(rect.w) * size_t(rect.h)
                        * size_t(pTexture->BytesPerPixel))),
                    pTexture->GetPitch());
            }

            pTexture->SetStatus(ImTextureStatus_OK);
        }
        else if (pTexture->Status == ImTextureStatus_WantDestroy)
        {
            const bgfx::TextureHandle handle = textureHandleFromId(pTexture->GetTexID());

            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }

            pTexture->SetTexID(ImTextureID_Invalid);
            pTexture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void GameImGuiBgfxRenderer::renderDrawData(const ImDrawData *pDrawData)
{
    if (!m_initialized || pDrawData == nullptr)
    {
        return;
    }

    const int framebufferWidth =
        static_cast<int>(pDrawData->DisplaySize.x * pDrawData->FramebufferScale.x);
    const int framebufferHeight =
        static_cast<int>(pDrawData->DisplaySize.y * pDrawData->FramebufferScale.y);

    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return;
    }

    const bgfx::Caps *pCaps = bgfx::getCaps();
    float projectionMatrix[16];
    bx::mtxOrtho(
        projectionMatrix,
        pDrawData->DisplayPos.x,
        pDrawData->DisplayPos.x + pDrawData->DisplaySize.x,
        pDrawData->DisplayPos.y + pDrawData->DisplaySize.y,
        pDrawData->DisplayPos.y,
        0.0f,
        1000.0f,
        0.0f,
        pCaps->homogeneousDepth);

    bgfx::setViewName(ImGuiViewId, "GameImGui");
    bgfx::setViewMode(ImGuiViewId, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(
        ImGuiViewId,
        0,
        0,
        static_cast<uint16_t>(framebufferWidth),
        static_cast<uint16_t>(framebufferHeight));
    bgfx::setViewTransform(ImGuiViewId, nullptr, projectionMatrix);
    bgfx::touch(ImGuiViewId);

    for (int commandListIndex = 0; commandListIndex < pDrawData->CmdListsCount; ++commandListIndex)
    {
        const ImDrawList *pDrawList = pDrawData->CmdLists[commandListIndex];
        const uint32_t vertexCount = static_cast<uint32_t>(pDrawList->VtxBuffer.size());
        const uint32_t indexCount = static_cast<uint32_t>(pDrawList->IdxBuffer.size());

        if (bgfx::getAvailTransientVertexBuffer(vertexCount, m_vertexLayout) < vertexCount
            || bgfx::getAvailTransientIndexBuffer(indexCount, sizeof(ImDrawIdx) == 4) < indexCount)
        {
            break;
        }

        bgfx::TransientVertexBuffer transientVertexBuffer = {};
        bgfx::TransientIndexBuffer transientIndexBuffer = {};
        bgfx::allocTransientVertexBuffer(&transientVertexBuffer, vertexCount, m_vertexLayout);
        bgfx::allocTransientIndexBuffer(&transientIndexBuffer, indexCount, sizeof(ImDrawIdx) == 4);

        std::memcpy(transientVertexBuffer.data, pDrawList->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
        std::memcpy(transientIndexBuffer.data, pDrawList->IdxBuffer.Data, indexCount * sizeof(ImDrawIdx));

        bgfx::Encoder *pEncoder = bgfx::begin();

        for (const ImDrawCmd &drawCommand : pDrawList->CmdBuffer)
        {
            if (drawCommand.UserCallback != nullptr)
            {
                drawCommand.UserCallback(pDrawList, &drawCommand);
                continue;
            }

            if (drawCommand.ElemCount == 0)
            {
                continue;
            }

            ImVec4 clipRect = drawCommand.ClipRect;
            clipRect.x = (clipRect.x - pDrawData->DisplayPos.x) * pDrawData->FramebufferScale.x;
            clipRect.y = (clipRect.y - pDrawData->DisplayPos.y) * pDrawData->FramebufferScale.y;
            clipRect.z = (clipRect.z - pDrawData->DisplayPos.x) * pDrawData->FramebufferScale.x;
            clipRect.w = (clipRect.w - pDrawData->DisplayPos.y) * pDrawData->FramebufferScale.y;

            if (clipRect.x >= framebufferWidth || clipRect.y >= framebufferHeight || clipRect.z <= 0.0f
                || clipRect.w <= 0.0f)
            {
                continue;
            }

            const uint16_t scissorX = static_cast<uint16_t>(std::max(clipRect.x, 0.0f));
            const uint16_t scissorY = static_cast<uint16_t>(std::max(clipRect.y, 0.0f));
            const uint16_t scissorWidth =
                static_cast<uint16_t>(std::max(clipRect.z - static_cast<float>(scissorX), 0.0f));
            const uint16_t scissorHeight =
                static_cast<uint16_t>(std::max(clipRect.w - static_cast<float>(scissorY), 0.0f));

            if (scissorWidth == 0 || scissorHeight == 0)
            {
                continue;
            }

            const ImTextureID textureId = drawCommand.GetTexID();

            if (textureId == ImTextureID_Invalid)
            {
                continue;
            }

            const bgfx::TextureHandle textureHandle = textureHandleFromId(textureId);

            pEncoder->setScissor(scissorX, scissorY, scissorWidth, scissorHeight);
            pEncoder->setState(
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            pEncoder->setTexture(0, m_textureSamplerHandle, textureHandle);
            pEncoder->setVertexBuffer(
                0,
                &transientVertexBuffer,
                static_cast<uint32_t>(drawCommand.VtxOffset),
                vertexCount - static_cast<uint32_t>(drawCommand.VtxOffset));
            pEncoder->setIndexBuffer(
                &transientIndexBuffer,
                static_cast<uint32_t>(drawCommand.IdxOffset),
                static_cast<uint32_t>(drawCommand.ElemCount));
            pEncoder->submit(ImGuiViewId, m_programHandle);
        }

        bgfx::end(pEncoder);
    }
}
}
