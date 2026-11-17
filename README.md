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

从仓库根目录构建（Qt 6.5+；Garmin 需要 Boost 头文件和 Qt Network，桌面界面还需要 QMapLibre Location）：

```sh
git submodule update --init tools/tilemaker
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

默认构建内置 tilemaker 转换程序，需要 Boost filesystem/program_options/system、
Lua（或 LuaJIT）、Shapelib、RapidJSON 和 SQLite 开发包。只需要查看地图时可以设置
`-DVECTORUNE_WITH_TILEMAKER=OFF`。转换控制器使用 Qt Concurrent 在后台校验输出。

数据目录通过 `VECTORUNE_DATA_DIR` 配置；未配置时依次查找当前工作目录的
`data/`、安装目录的 `share/vectorune/`、系统应用数据目录。目录内使用
`maps/` 和 `gpx/`。也可用 `--map <文件>` / `--mbtiles <文件>` 显式指定。
找不到默认地图仍可进入空白地图并使用“加载地图”。

目前运行时支持 Garmin IMG、兼容的矢量 MBTiles、栅格 MBTiles 和 GPX。
OSM PBF 是转换工具的输入，不是直接显示格式。转换入口：

Viewer 顶部 **地图 → 转换 OSM 地图…**：选择 `.osm.pbf`，勾选地图要素，选择
标准/浅色/深色风格及 MBTiles、PMTiles 或 XYZ 瓦片目录。转换期间可以继续使用地图，
也可以取消任务。MBTiles 转换成功后可直接打开；PMTiles 和 XYZ 当前仅支持导出。
风格不会改变矢量数据：MBTiles 会记录风格供 Viewer 重开时使用，所有输出均附带
MapLibre 样式 JSON。文件输出旁生成 `<文件名>.style.json`，目录输出内生成 `style.json`。
样式中的本地数据和字体路径在其他应用或主机使用时需要按部署位置调整；PMTiles
样式需要读取端支持 `pmtiles://` 协议，XYZ 目录需要通过瓦片服务提供访问。

此 GUI 使用 OSM 数据本身，不下载全球海岸线等辅助数据。建筑图层保留 tilemaker
生成的 `render_height` / `render_min_height` 属性（缺少原始高度时可能根据楼层估算）；
三种内置风格默认二维显示。打开带建筑高度的 MBTiles 后，可点击右上角 **3D**
按钮显示立体建筑，再次点击恢复二维俯视。中文标签使用随 Viewer 部署的 Noto CJK 字形。
输出路径须尚不存在，失败或取消会清理临时结果，不覆盖已有文件。

原命令行入口仍可用于自定义 config / Lua 和辅助数据：

```sh
python3 tools/convert_osm.py data/Beijing.osm.pbf output/beijing.mbtiles \
  --tilemaker build/tools/tilemaker/tilemaker
```

该入口需要先构建 tilemaker 或通过 `--tilemaker` 指定可执行文件。
OSM PBF → Garmin IMG 尚未接入；条件时段路由也尚未实现。

Garmin 首次加载预生成 SQLite 几何空间/层级索引和三种出行模式的紧凑路网。
显示时按瓦片范围读取几何，经本机回环服务提供给 MapLibre；规划时直接映射路网文件。
缓存命中无需重新导入或建图，运行时不再读取整份 GeoJSON。首次预处理仍有全图解析和建图开销。

详见 [Mapviewer 使用说明](mapviewer/README.md)、
[重构实施记录](docs/refactoring-progress-2026-11-09.md)、
[几何分块与紧凑路网](docs/spatial-routing-index-2026-11-09.md) 和
[ARM 构建与验收](docs/platforms.md)。
