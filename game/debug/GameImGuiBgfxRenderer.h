#pragma once

#include <bgfx/bgfx.h>

struct ImDrawData;

namespace OpenYAMM::Game
{
class GameImGuiBgfxRenderer
{
public:
    bool initialize();
    void shutdown();
    void newFrame();
    // Honours ImGui's texture requests (create/update/destroy) before the draw data is submitted.
    // Without this the 1.92 font atlas locks after the first frame and every later glyph — which is
    // every CJK glyph, since those only ever arrive on demand — is silently dropped.
    void updateTextures();
    void renderDrawData(const ImDrawData *pDrawData);

private:
    static bgfx::VertexLayout createVertexLayout();

    bgfx::ProgramHandle m_programHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_textureSamplerHandle = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_vertexLayout;
    bool m_initialized = false;
};
}
