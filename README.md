# OpenYAMM 简体中文定制分支

[OpenYAMM](https://github.com/pjasicek/openyamm) 是《魔法门 VI／VII／VIII》的开源重制引擎（C++20、bgfx、
SDL3）。上游的文本系统是「单字节 CP1252 + 256 个字形槽」，天生装不下中文。

本仓库是上游的 fork，**唯一目的就是让引擎能正确显示中文**，并把这份改造以 commit 的形式固定下来，
好让之后继续跟上游同步时改动不会丢。

| | |
|---|---|
| 上游基线 | tag `v1.20`（commit `1add1d3`） |
| 配套补丁版本 | 1.20.0 |
| 上游原始说明 | [上游 README](https://github.com/pjasicek/openyamm) |
| 汉化语料与发布包 | [vnimy/openyamm-zh-cn](https://github.com/vnimy/openyamm-zh-cn) |
| 许可 | GPL-3.0（见 [LICENSE](LICENSE)） |

> 这个仓库**不含译文数据，也不含任何游戏资产**。它只提供「能渲染中文的引擎」。
> 译文、中文字体、打包脚本都在 [openyamm-zh-cn](https://github.com/vnimy/openyamm-zh-cn)。

---

## 资源包（游戏内容）怎么下载、怎么用

游戏资产（PC 约 5.41 GiB）**不在 GitHub 上**，由作者自建频道提供；GitHub Releases 里只有几十 MB 的「启动器 + 程序」小包。

| 平台 | 频道清单 | 包数 / 总大小 |
|---|---|---|
| PC（windows-x64） | `https://jasicek.net/openyamm/updates/channels/stable/windows-x64.json` | 166 个 / 5.41 GiB |
| Android（arm64） | `https://jasicek.net/openyamm/updates/channels/stable/android-arm64.json` | 112 个 / 3.24 GiB |

- 单个包：`https://jasicek.net/openyamm/updates/packs/<sha256>.zip`（文件名就是内容哈希，不可变、支持 Range 断点续传）
- 程序包：`https://jasicek.net/openyamm/updates/apps/<sha256>.zip` / `.apk`；玩家下载页 `https://jasicek.net/openyamm/downloads/1.20/`
- 清单带 Ed25519 签名（同名 `.sig`），公钥编在游戏里；`jasicek.net` 直连即可，不需要代理。

### PC

1. **全自动（推荐）**：主菜单点 *Update to OpenYAMM 1.20*，按清单只下缺的包，落到 `<安装目录>/assets/packs/`，
   同时写好 `app/1.20/index.json` 并切换 `app/current`。
2. **自动检查开关**：`settings.ini`
   ```ini
   [updates]
   check_for_updates=ask     ; ask | auto | off
   url=https://jasicek.net/openyamm/updates/
   ```
3. **命令行**（不开界面）：`openyamm.exe --headless-update [频道URL]`
4. **手动 / 离线**：看 `app/1.20/index.json` 里每个包的 `file` 字段（如 `engine.core-4d36264717f50bbd.zip`），
   把对应的 `<sha256>.zip` 下载后改名放进 `assets/packs/`；整个 `assets/packs` 目录也可以直接拷到另一台机器。

### Android

1. 首次启动按清单下载到 `Android/data/org.openyamm.android/files/content/`（`index.json` + `packs/*.zip`）。
2. **离线 / 搬家**：本仓库 `build-android/make_content_bundle.ps1` 把这个目录打成一个 zip，拷进手机解压到同一路径即可。
3. **增量**：1.19 → 1.20 的安卓内容 112 个包里 99 个完全相同，只有 13 个变化（合计 0.141 GiB），
   已经下过 1.19 内容的手机只需换这 13 个包和 `index.json`。

---

## 一、为什么非改引擎不可

上游三道硬约束，任何一条都足以让中文显示不出来：

| 约束 | 位置 | 后果 |
|---|---|---|
| 字形按「固定 16×16 格子 + 单字节索引」取 | `engine/FontAsset` | 一个字节只能指到 256 个槽，中文有几万个字 |
| 文本按 CP1252 解码 | 文本读取与 `MenuDesignScreen` | UTF-8 的三个字节被当成三个乱码字符，界面上是方块或问号 |
| 折行按单字节等宽推算 | `MenuScreenBase` / `GameplayHudCommon` | 中文没有空格，整句塞进按钮溢出；标点被顶到行首 |

所以只做「翻译表」是没用的：**原版引擎拿到 UTF-8 字节会当成 CP1252 乱码渲染**。
必须把引擎的文本层从「单字节 + 字形槽」换成「UTF-8 + 按 Unicode 码点取字形」，再让它能按中文规则断行。

---

## 二、本分支定制了什么

共 93 个文件（86 个改动 + 7 个新增），分七块。

### 1. 文本层

| 文件 | 改动 |
|---|---|
| `game/ui/UiText.{h,cpp}` | **新增**。覆盖层：启动时读 `data_tables/english/ui_text.txt`，把「原文 → 译文」装进内存 |
| `engine/TextLayout.{h,cpp}` | **新增**。按码点切分的中文断行：行首不放避头标点（`，。、？！：；）》」』`），行尾不放避尾标点（`（《「『`） |
| `engine/FontAsset.{h,cpp}` | 字形表从「单字节索引」改为「Unicode 码点 → 字形」；支持 FreeType 轮廓字体按需光栅化 |
| `engine/TextTable.cpp` | 表格解析允许单元格加引号、引号内 `""` 表示字面引号，与 openyamm-zh-cn 的 `tools/texttable.py` 语义保持一致 |

`UiText` 的四个接口，是这个分支所有布局问题的答案：

| 接口 | 用途 |
|---|---|
| `translate(text)` | 整串精确匹配；匹配不到再按 `*` 结尾的前缀规则匹配（`Update to OpenYAMM *` 这类动态串） |
| `translateTemplate(text, params)` | 先查表拿到「模板」，再填 `{占位符}`。用来处理中英文语序差异 |
| `titleWithName(name, title)` | 英文「某人 the 某某」改成中文语序「某某某人」 |
| `reportUnresolvedUiText(text)` | 采集没命中的界面字符串，调试控制台 `uireport` 导出清单，用来找漏译 |

还有 `normalizeQuotes`：老数据表与 lua 里的引号常是 CP1252 的孤立字节（`0x91`/`0x92` 弯引号、
`0x96`/`0x97` 破折号、`0x85` 省略号）。合法 UTF-8 原样保留，孤立字节按 CP1252 还原，
否则查表永远差一个引号，界面上还画成方块。

### 2. 绘制与排版

| 文件 | 改动 |
|---|---|
| `game/ui/MenuScreenBase.{h,cpp}` | 绘制与测宽改成按码点取字形，去掉固定 16×16 格子；光栅高度按显示尺寸量化（≤20 步长 2、≤40 步长 4、>40 步长 8，上限 96），避免为每个像素尺寸各建一套图集 |
| `game/ui/GameplayHudCommon.{h,cpp}` | HUD 的绘制、测宽、截断、折行全部走 UTF-8 与码点；`renderLayoutLabel`／`renderHudFontLayer`／`wrapHudTextToWidth` 三处**先查覆盖表再测量**——顺序反了布局仍按英文宽度算 |
| `game/ui/MenuDesignScreen.cpp` | 去掉 UTF-8 → CP1252 的转换，文本原样透传 |
| `game/ui/screens/NewGameScreen.cpp` | 角色创建界面折行改成码点安全、按中文规则断行；随机名池改从 mod 的 `pc_names.txt` 读 |
| `game/ui/GameplayUiController.cpp` | 进游戏时装载覆盖表（原先只有菜单装载，游戏内文案全漏） |

### 3. 拼接文案模板化

英文靠空格拼出来的句子，直接翻译会得到「青铜戒指 土系抗性」这种语序。
解决办法是把拼接挪进模板，语序写进译文表。25 个文件接了 `UiText`，关键几处：

| 文件 | 处理 |
|---|---|
| `game/items/ItemEnchantRuntime.cpp` | 物品全名 = 基础名 + 词缀。改成 `translateTemplate("{item} {suffix}")`，译文表里写 `"{suffix}{item}"`，于是 `Bronze Ring` + `of Earth Resistance` 变成「御土青铜戒指」 |
| `game/events/EventRuntime.cpp` | NPC 对话正文不在任何数据表里，是内容包 lua 的 `evt.SetMessage(...)` 字面量。在 lua 绑定层统一过一遍覆盖表（`luaSetMessage`／`luaSimpleMessage`／`luaStatusText`） |
| `game/gameplay/HouseServiceRuntime.cpp` | 买卖、鉴定、修理的台词共 17 处改模板，例如 `Bought {item} for {price} gold.` |
| `game/ui/GameplayPartyOverlayRenderer.cpp`、`game/outdoor/OutdoorGameView.cpp` | 物品检视详情的 `Armor`／`Charges`／`Attack-Damage`／`Shoot-Damage`／`Power`／`Uses`／`Duration` 全部改模板键（这两份是重复实现，必须同步改） |
| `game/ui/GameplayHudOverlaySupport.cpp` | 法术与增益剩余时长、个人增益名、队伍增益弹窗标题与正文接覆盖表 |
| `game/gameplay/CorpseLootRuntime.cpp`、`GameplayLootSummaryRenderer.cpp`、`GameplayScreenController.cpp` | 战利品汇总、背包切换、多袋溢出提示等 1.19 新增面板 |
| `game/gameplay/MasteryTeacherDialog.cpp` | 技能教师「技能 - 精通度」 |
| `game/tables/MergedBaseTables.cpp`、`HouseInteraction.cpp` | 人名 + 头衔拼接走 `titleWithName`；房屋名与动作标签 |

### 4. 安卓端

Android 版必须重编引擎，另外要让 Android 也加载 `mods/`：上游用 `#if !defined(__ANDROID__)`
排除了 `modsRoot`，APK 资产分支也没有挂载点。共四处：

| 文件 | 改动 |
|---|---|
| `game/app/main.cpp` | 移除 `modsRoot` 的 Android 排除 |
| `engine/AssetFileSystem.cpp` | APK 资产分支加上 `mountMods()`，让 mods 叠在 APK 内 pack 之上 |
| `android/app/src/main/cpp/openyamm_android_entry.cpp` | 启动前把 APK 内的 mods 解到工作目录 |
| `game/audio/MusicStream.cpp` | 无 FFmpeg 时的兜底实现 |

### 5. 调试控制台

调试控制台是纯 ImGui 画的，上游用的是内置 ASCII 图集，所以中文全变 `?`。

| 文件 | 改动 |
|---|---|
| `game/debug/ImGuiCjkFont.{h,cpp}` | **新增**。从 `cjk_glyphs.txt`（十六进制码点）加 `0x20–0xFF` 拼出 `ImWchar` 区间，加载中文字体 |
| `game/debug/GameImGuiBgfxRenderer.{h,cpp}` | 声明 `ImGuiBackendFlags_RendererHasTextures`，每帧处理 `ImTextureStatus` 的 WantCreate／WantUpdates／WantDestroy（ImGui 1.92 的动态字体图集契约） |
| `game/app/GameApplication.cpp` | 装载中文字体、移动端触控参数、每帧 `updateTextures()`；新增 `uireport on|off|dump|clear` 命令 |

> **不要退回静态图集**：一旦没声明 `RendererHasTextures`，1.92 的字形懒加载会在首帧后锁死，
> 中文全部变回 `?`。契约细节见 openyamm-zh-cn 的 `docs/engine-changes.md`。

### 6. 构建与测试

| 文件 | 改动 |
|---|---|
| `cmake/Ffmpeg.cmake`、`cmake/RewriteMsysPaths.cmake` | 后者新增。Windows 原生构建时把 FFmpeg 脚本产出的 MSYS 路径改写成盘符路径；已存在的库跳过重建 |
| `game/CMakeLists.txt`、`game/audio/HouseVideoPlayer.cpp` | 新增 `OPENYAMM_HOUSE_VIDEOS` 开关（默认开），关掉时提供空实现 |
| `tests/FontAssetTests.cpp`、`tests/GameplayUiRuntimeTests.cpp`、`tests/CMakeLists.txt` | 字体测试改用码点接口；新增「CJK 字面烘焙」用例（设了 `OPENYAMM_CJK_ASSETS` 才跑） |
| `game/scenario/ScenarioRunner.cpp` | 无头场景：`report_items` 抽成独立断言；带 `mods/` 的场景要传 6 参数 `AssetFileSystem::initialize`（含 `installedContent`）才挂载 mod |

### 7. 存档迁移

| 文件 | 改动 |
|---|---|
| `game/app/GameSession.cpp` | `migrateLocalizeJournalTrainerNotes()`：旧存档的日志里存着英文的「技能 - 精通度」串，读档时按当前覆盖表重写一遍；幂等，且只在覆盖表存在时执行 |

---

## 三、和 openyamm-zh-cn 怎么配合（汉化流程）

两个仓库是分开的，装到一起才是一个可玩的汉化版：

```
D:/Projects/MM678/
├── openyamm/              引擎源码（本仓库）  ──编──▶  openyamm.exe
└── openyamm-zh-cn/        译文语料 + 工具     ──生成──▶  mods/engine/…

安装目录（上游官方安装 + 覆盖补丁）
  app/1.19/openyamm.exe        ← 本仓库编出来的引擎
  assets/packs/*.zip           ← 上游官方内容包（约 5 GB，首次启动下载）
  mods/engine/…                ← zh-cn 生成的译文表与中文字体
```

引擎的挂载顺序是「官方 pack → `mods/`」，后者覆盖前者。所以：

- **只有 exe 和 mods 版本对齐才有意义**。只换 mods 不换 exe → 乱码；只换 exe 不换 mods → 全英文。
- `mods/` 只在「打包资产」布局下挂载（`assets/packs/*.zip` 或 APK 内的 pack）。
  用 `assets_dev/` 目录跑的开发版**不会**挂载 mods，别拿它验补丁。

### 自己加一条译文

1. 改 `openyamm-zh-cn/corpus/overrides/` 里的语料（`manual_zh.tsv`、`lua_dialog_zh.tsv` 等）；
2. `python tools/make_ui_text.py` 合成 `corpus/overrides/ui_text.txt`（产出物勿手改）；
3. `python tools/build_patch.py --openyamm ../openyamm --out build/mods`；
4. 重启游戏——`UiText::load()` 只跑一次，改完表必须重启；
5. 查漏：`python tools/check_literals.py --openyamm ../openyamm`（源码里还有哪些硬编码文案没进表）。

译文表的匹配顺序是「精确整串 → 前缀规则 → 占位符模板」，所以同一个英文串可以同时有精确条目和前缀规则。

---

## 四、跟上游同步（本仓库存在的意义）

改造已经是一个个 commit，所以同步就是普通的 `git merge`，不需要重新「移植」：

```bash
git remote add upstream https://github.com/pjasicek/openyamm.git   # 只需一次
git fetch upstream --tags
git switch -c fork/1.20 upstream/v1.20
git merge fork/1.19          # 或者在 fork/1.19 上直接 merge v1.20
```

冲突热点（改动集中的地方）：

| 文件 | 冲突原因 |
|---|---|
| `game/ui/MenuScreenBase.cpp` | 上游改绘制/测宽会直接撞上我们的码点版本 |
| `game/ui/GameplayHudCommon.cpp` | HUD 折行与测宽 |
| `game/ui/screens/LoadGameScreen.cpp` | 存档列表几乎每个大版本都重做 |
| `game/tables/MergedBaseTables.cpp` | 人名 + 头衔拼接 |
| `engine/FontAsset.cpp` | 字形接口 |
| `README.md` | 本分支把它换成了中文；合并时保留我们的版本即可 |

合并后的检查清单：

- [ ] `python ../openyamm-zh-cn/tools/check_literals.py --openyamm .` —— 上游新增的 `game/ui/*.cpp` 里往往带新硬编码文案
- [ ] 跑无头场景（见下节），确认没把运行时搞坏
- [ ] 留意表名变化：1.17 → 1.19 就把 `english/global.txt` 改名成了 `Global.txt`
- [ ] `engine/FontAsset` 的字形接口、`MenuScreenBase` 的光栅高度量化有没有被上游重构

---

## 五、构建

### 5.1 PC（Windows x64，MinGW-w64 + Ninja）

依赖走 FetchContent。本机把依赖预解压到 `toolchains/deps/`，配置期不联网：

```bash
cmake -S . -B build-mingw -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=D:/toolchains/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=D:/toolchains/mingw64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=D:/toolchains/mingw64/bin/ninja.exe \
  -DCMAKE_SUPPRESS_REGENERATION=ON \
  -DOPENYAMM_HOUSE_VIDEOS=ON \
  -DFETCHCONTENT_SOURCE_DIR_SDL3=D:/toolchains/deps/sdl3 \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE=D:/toolchains/deps/freetype \
  # 其余同名开关同理：BGFX BIMG BX FFMPEG FMT IMGUI LUA MONOCYPHER
  # PHYSFS SPDLOG YAML-CPP OPENYAMM_ETC2COMP OPENYAMM_ETCPAK OPENYAMM_ZSTD
  # 完整清单见 build-mingw/CMakeCache.txt 里的 FETCHCONTENT_SOURCE_DIR_*

cmake --build build-mingw --target openyamm -j25
```

产物：`build-mingw/game/openyamm.exe`（约 54 MB）。

两个必须知道的坑：

- `-DCMAKE_SUPPRESS_REGENERATION=ON`：不加的话 Ninja 会卡在 `Re-checking globbed directories...`。
  代价是**新增源文件后必须手动重跑一次 `cmake -B build-mingw`**，否则新文件不进构建，报错是 `undefined reference`。
- 源码树被移动或改名后，旧的 build 目录里 CMakeCache.txt 记的是旧路径，要整体重配。

### 5.2 Android（arm64）

```bash
# 1) 桌面先编一次，安卓构建要复用桌面产出的 openyamm_shaderc.exe
bash build-android/configure-android-1.19.bat   # MinGW Makefiles + NDK r28c + arm64-v8a
cd build-android/native-1.19 && make -j8

# 2) 重打 APK：把原生库、dex、mods 塞进官方 APK 并重签名
powershell -NoProfile -ExecutionPolicy Bypass -File build-android/pack_zh_apk.ps1 -Version 1.19
```

Android 侧用 `-G "MinGW Makefiles"` 而不是 Ninja：Ninja 在这台机器上会挂（build lock/jobserver）。

### 5.3 无头场景验证（不开窗口）

```bash
cd <安装目录>
./app/1.19/openyamm.exe --headless-run-scenario <绝对路径>.yml --scenario-no-color
```

判定标准：退出码 0 且输出里没有 `ASSERT fail`。现有 7 个场景（`baa_letter_test`、`isaac_shop_test`、
`item_inspect_test`、`maria_moon_test`、`npc_dialog_test`、`skill_name_test`、`zh_text_coverage`）在
`openyamm-zh-cn/tests/scenarios/`。

> 注意：这些场景**不绘制 HUD**。字体、字号、模糊度这类纯像素问题，它们验不出来。

---

## 六、发布内容

| 产物 | 说明 |
|---|---|
| `OpenYAMM-1.19-zh_cn.zip` | PC 覆盖包（zip 根目录只有 `openyamm.exe`、`mods/`、`安装说明.txt`）：把 `openyamm.exe` 放到 `app/1.19/`（原文件备份成 `openyamm.exe.orig`），`mods/` 放到安装根目录。**覆盖到已装好的 OpenYAMM 1.19 上**，不是独立游戏 |
| `OpenYAMM-1.19-android-arm64-zh.apk` | Android arm64 完整包：官方 APK + 中文引擎 + mods，自签名 |

游戏资产受版权保护，两个产物都**不含** `assets/packs/*.zip`：玩家需要自己有一份正版游戏数据。

---

## 七、已知问题

- **字体模糊**：设置、存读档、主菜单、建队界面清晰；**日志正文、NPC 对话内容、对话人名与选项、HUD 标签仍然偏糊**。
  根因已定位但没修完，实测与回滚记录见 openyamm-zh-cn 的 `docs/text-rendering-pitfalls.md`。
  一句话版：游戏里有**三条**独立的文本绘制路径，日志正文走的那条是**缩小绘制**（24 px 图集缩到约 15 px）；
  而且 `GameplayUiRuntime::renderHudFontLayer` 等 4 处查表会把字体句柄里的 `fontHeight` 丢掉。
  只改其中一条路径不会有任何变化；按尺寸换栅格又会撞上那 4 处查表，出现「字比阴影小」或「大得离谱」。
- 译文没有 100% 覆盖（覆盖表 6141 条），仍有一些界面边角会冒英文；缺口清单见 openyamm-zh-cn 的 `docs/progress.md`。
- 中文断行是按标点规则的简单实现，没有做完整的 UAX #14。

---

## 八、许可

本仓库是 [pjasicek/openyamm](https://github.com/pjasicek/openyamm) 的 fork，沿用上游的 **GPL-3.0**
（见 [LICENSE](LICENSE)）；原有版权归上游作者所有，本分支的改动同样以 GPL-3.0 发布。

《魔法门 VI／VII／VIII》的资产、数据与商标归 Ubisoft 所有，本项目不附带、不分发这些内容。
