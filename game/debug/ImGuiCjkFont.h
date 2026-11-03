#pragma once

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace OpenYAMM::Game
{
// 调试控制台由 ImGui 绘制，而 ImGui 的字体图集默认只带 ASCII，中文没有字形就画成问号。这里把汉化补丁的
// CJK 字体装进图集，码位表与引擎字体共用 cjk_glyphs.txt，两套图集覆盖的字符因此永远一致。
// 必须在图集第一次构建（GetTexDataAsRGBA32）之前调用，且只调用一次。
// pixelHeight 是字形**实际显示**的像素高度：图集按它烘焙，运行时不再放大，放大位图只会糊。
void installImGuiCjkFont(const OpenYAMM::Engine::AssetFileSystem &assetFileSystem, float pixelHeight);
}
