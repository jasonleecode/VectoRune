# Navit Mac 预览记录

2026-11-09，在 Mac ARM64 上构建并启动了官方 Navit trunk `f8de56c`（版本标记 0.5.7）。源码来自 [navit-gps/navit](https://github.com/navit-gps/navit)，独立存放在 `build/navit-preview/source/`；没有替换 `experiments/navit/dev` 中的历史 0.0.4，也没有接入 VectoRune 的主程序。

## 本机启动

打开 `build/navit-preview/Navit Preview.app`，或者从仓库根目录执行：

```sh
bash experiments/scripts/run-navit-preview.sh
```

此脚本仅启动已构建的本地预览，不会下载或构建依赖。`build/` 不提交到 Git，因此新检出的仓库没有这个应用。

实际验证了北京地图、建筑、中文路名和地图按钮显示。点击地图进入内置菜单，左下/右下按钮缩小/放大。使用 Demo 车辆，不连接真实 GPS；本轮没有进行 Navit 实地导航验收。

## 构建与数据

- 图形组合：Navit internal GUI + SDL 后端，使用 sdl12-compat 和本机原生 SDL2 2.32.10。
- 安装依赖：Homebrew 的 sdl12-compat、protobuf-c；其依赖升级包含 SDL、protobuf 和 abseil。SDL_image 使用官方 `libsdl-org/SDL_image` 仓库的 SDL-1.2 分支，单独构建到 `build/navit-preview/deps/`。
- CMake 调整：优先搜索 Navit 自己的头文件，避免 Homebrew 的同名 `event.h`；非 bundle 构建改用标准 `start.c`；显式指定 Homebrew 链接目录；关闭本次不需要的 Cocoa、OpenGL、SVG debug、null graphics 和 shapefile 插件。
- app 添加本地核心库 rpath、复制 SDL 库并做 ad-hoc 签名。它仍依赖本机 Homebrew，不能直接作为独立发行包分发。
- 输入 `data/maps/Beijing.osm.pbf`，通过 Navit 的 maptool 转为 `build/navit-preview/runtime/beijing.bin`，不使用 Mapviewer 的 Garmin 或 MBTiles 渲染链路。

地图转换命令（在 `build/navit-preview/runtime/` 下执行）：

```sh
../cmake/navit/maptool/maptool --protobuf --slice-size 134217728 \
  --input-file ../../../data/maps/Beijing.osm.pbf beijing.bin
```

应用配置位于 `runtime/user/navit.xml`，脚本使用 `runtime/navit.xml`；用户状态写入 `runtime/user/`。上述路径均相对于 `build/navit-preview/`。地图布局使用 Arial Unicode MS 显示中文。

Navit 的配置解析器要求 XInclude 使用 `xi` 前缀。将其改写为 ElementTree 默认的 `ns0` 会导致配置解析失败，窗口出现黑屏；本次已修正。

详细配置保存在本机 `build/navit-preview/cmake/CMakeCache.txt`，构建日志为 `build.log`，转换日志为 `runtime/map-convert.log`，应用日志为 `runtime/app.log`。本次记录和启动脚本不等于已经建立了可复现的跨机器 Navit 发布流程。
