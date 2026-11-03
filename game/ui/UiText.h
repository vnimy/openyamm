#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

// 菜单 UI 的文本覆盖层。OpenYAMM 重做的界面把文案硬编码在源码里，不走数据表，因此这里
// 按「原文 -> 译文」整串精确替换；覆盖表由汉化补丁以 data_tables/english/ui_text.txt 提供。
namespace UiText
{
// 从 data_tables/english/ui_text.txt 读取覆盖表：每行「原文<TAB>译文」，# 开头为注释，
// 译文中 \n 表示换行。只加载一次，之后调用为空操作。
void load(const OpenYAMM::Engine::AssetFileSystem &assetFileSystem);

// 整串精确匹配：命中返回译文，否则原样返回。未加载时恒等返回。
std::string translate(const std::string &text);

// 模板翻译：先按整串查表（如 "{bonus_points} points left"），再把模板里的每个
// {占位符} 替换为对应值。用于源码里先拼接数字再交给 UI 层的动态文案。
std::string translateTemplate(const std::string &text,
                              std::initializer_list<std::pair<std::string, std::string>> params);

// 覆盖表是否包含条目（加载过文件且非空）。
bool hasOverrides();

// 找出「没接钩子」的界面英文。绘制入口每帧调用 reportUnresolvedUiText()，只有
// setUnresolvedReportEnabled(true) 之后才真正收集；未开启时每次调用只是一次 bool 判断。
void setUnresolvedReportEnabled(bool enabled);
bool unresolvedReportEnabled();
void reportUnresolvedUiText(const std::string &text);
// 取出并清空已收集的清单。上限 400 条，满了就不再收。
std::vector<std::string> takeUnresolvedUiText();

// 「某人 the 某某」的人名+头衔拼接。英文语序在中文里读不通，装上覆盖表时改用「名称·头衔」，
// 英文环境保持原样。
std::string titleWithName(const std::string &name, const std::string &title);
}
