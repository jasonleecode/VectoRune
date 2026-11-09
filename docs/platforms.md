# 构建、安装与 ARM 验收

当前已验证平台是 Mac ARM64。用户暂未确定目标 ARM 板，因此这里提供构建接口和验收项目，不声明已满足嵌入式性能指标。

## 构建范围

根目录 CMake 支持：

| 开关 | 默认值 | 用途 |
| --- | --- | --- |
| `VECTORUNE_BUILD_VIEWER` | ON | Qt Quick + MapLibre 界面 |
| `VECTORUNE_WITH_GARMIN` | ON | Garmin 适配、导入和路由工具 |
| `VECTORUNE_WITH_MBTILES` | ON | MBTiles 适配和名称索引工具 |
| `BUILD_TESTING` | OFF | 回归测试 |

核心和工具仍依赖 Qt Core、Positioning、Sql；不构建界面时不要求 QMapLibre、Qt Quick 或 Qt Location。`MAPVIEWER_BUILD_TESTS` 保留为旧测试开关兼容入口。旧的 `cmake -S mapviewer` 会转到统一构建，产物目录与旧版本不同，请使用根目录入口。旧 qmake 工程明确报出迁移提示。

只构建核心、MBTiles 和测试的例子：

```sh
cmake -S . -B build/core-only -DCMAKE_BUILD_TYPE=Release \
  -DVECTORUNE_BUILD_VIEWER=OFF -DVECTORUNE_WITH_GARMIN=OFF \
  -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/core-only --parallel 8
ctest --test-dir build/core-only --output-on-failure
```

## 安装布局

```sh
cmake --install build --prefix /absolute/path/to/stage
```

- `bin/`：主程序和地图导入、路由、搜索、检查程序。
- macOS 的 `.app/Contents/MacOS/` 同时包含运行所需辅助程序；MapLibre geoservices 插件进入 `.app/Contents/PlugIns/`。
- `bin/vectorune-convert-osm`：PC 端转换入口；配置位于 `share/vectorune/tilemaker/`。
- 地图数据没有自动打包。使用 `VECTORUNE_DATA_DIR`、命令行文件参数或文件选择器指定地图。

上述安装布局已在本机 staging 目录验证。它还不是独立于系统依赖的发行包：Qt、QMapLibre 动态库以及 Qt SQL/QML/定位等运行时插件仍需要目标 SDK/系统提供。Linux 安装规则已包含辅助程序，但未在目标 Linux 板上实际部署。

环境变量：

| 变量 | 含义 |
| --- | --- |
| `VECTORUNE_DATA_DIR` | 包含 `maps/`、`gpx/` 的数据目录 |
| `VECTORUNE_CACHE_DIR` | 缓存根目录；默认使用系统应用缓存目录 |
| `VECTORUNE_HELPER_DIR` | 辅助程序目录覆盖，便于 SDK/测试部署 |

## ARM64 Linux 交叉编译模板

`cmake/aarch64-linux.cmake` 要求明确提供 sysroot，并将库、头文件、CMake 包限制在目标 SDK 内，避免误用宿主机依赖。

```sh
cmake -S . -B build/arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake \
  -DVECTORUNE_SYSROOT=/absolute/path/to/target-sysroot \
  -DVECTORUNE_TOOLCHAIN_PREFIX=/absolute/path/to/toolchain/bin/aarch64-linux-gnu- \
  -DQT_HOST_PATH=/absolute/path/to/host-qt \
  -DCMAKE_PREFIX_PATH=/usr/local/qt-target \
  -DVECTORUNE_BUILD_VIEWER=OFF -DCMAKE_BUILD_TYPE=Release
```

这些是 SDK 路径占位值，不能原样执行。启用界面时，还需目标架构的 Qt Quick/Location、QMapLibre，以及适配目标 GPU/窗口系统的渲染后端。工具链模板尚未经实际目标 SDK 验证。

## 硬件确定后的验收

先记录 CPU、RAM、GPU/驱动、窗口系统、闪存、地图规模和更新方式，再确定数值预算：

1. Release 冷启动、首次导入、缓存命中加载时间。
2. 主程序、导入和路由进程各自及并发时的峰值内存。
3. 普通缩放/旋转/拖动的帧率与卡顿；最高密度城区和大范围视图。
4. 搜索首次建索引、后续查询、首次规划与偏航重算延迟。
5. 多次切图、异常退出、磁盘不足后的缓存容量与恢复。
6. 离线运行、系统定位不可用/拒绝/过期、路线限制回归。
7. 完全移除源码目录后，从安装目录启动并加载地图、搜索和规划。

本轮 Mac 结果仅作为工程基线，不转换成 ARM 验收承诺。
