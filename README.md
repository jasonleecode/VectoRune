# VectoRune
A compact embedded vector map engine

支持的地图格式
- garmin
- mapbox
- osmpbf

这是一个轻量化高性能的离线地图工具，包含地图和导航功能，支持多种地图格式，比如佳明地图、mbtiles地图，附带osm.pbf地图转换工具，可以将osm.pbf地图转为佳明地图和mbtiles等格式。支持gpx格式轨迹文件加载显示。整个地图工具适用于ARM处理器平台，对性能受限的设备友好。整个项目注重模块化和可扩展性，对不同地图格式的支持采用plugins形式。

lib目录下是各种地图格式的解析库
tools下是地图转换工具

## 当前目录与构建

```text
core/          地图会话、插件接口、缓存、名称/空间索引、通用路由和 GPX
plugins/       Garmin / MBTiles 格式适配、格式资源与编译期注册
mapviewer/     Qt Quick 界面和 MapLibre 渲染接入
lib/           第三方解析库
tools/         地图导入、检查、转换程序及 tilemaker 子模块
tests/         核心、格式、集成和界面回归测试
docs/          设计审视、实施进度和平台说明
experiments/   历史 navit、navitdrv、Web 实验，不参与主构建
```

从仓库根目录构建（Qt 6.5+，桌面界面还需要 QMapLibre Location）：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DQMapLibre_DIR="$PWD/../maplibre-native-qt/install/lib/cmake/QMapLibre"
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

Mac 启动：`open build/mapviewer/qml_location_mapviewer.app`。
不构建界面可设置 `-DVECTORUNE_BUILD_VIEWER=OFF`；格式可通过
`VECTORUNE_WITH_GARMIN`、`VECTORUNE_WITH_MBTILES` 分别裁剪。
GUI 测试需要桌面会话；无显示环境可用 `ctest --test-dir build -E mapviewer_interactions`。

数据目录通过 `VECTORUNE_DATA_DIR` 配置；未配置时依次查找当前工作目录的
`data/`、安装目录的 `share/vectorune/`、系统应用数据目录。目录内使用
`maps/` 和 `gpx/`。也可用 `--map <文件>` / `--mbtiles <文件>` 显式指定。
找不到默认地图仍可进入空白地图并使用“加载地图”。

目前运行时支持 Garmin IMG、兼容的矢量 MBTiles、栅格 MBTiles 和 GPX。
OSM PBF 是转换工具的输入，不是直接显示格式。转换入口：

```sh
python3 tools/convert_osm.py data/maps/Beijing.osm.pbf output/beijing.mbtiles
```

该入口需要先构建 tilemaker 或通过 `--tilemaker` 指定可执行文件。
OSM PBF → Garmin IMG 尚未接入；条件时段路由也尚未实现。

详见 [Mapviewer 使用说明](mapviewer/README.md)、
[重构实施记录](docs/refactoring-progress-2026-11-09.md) 和
[ARM 构建与验收](docs/platforms.md)。
