#include "router.h"
#include <QHash>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

namespace {
struct Edge {
    int destination, way;
    double distance, seconds;
    int token;
};
struct Node {
    QGeoCoordinate coordinate;
    QVector<Edge> edges;
};
quint64 coordinateKey(double lon, double lat) {
    auto x = qint32(std::llround(lon * 16777216.0 / 360)), y = qint32(std::llround(lat * 16777216.0 / 360));
    return (quint64(quint32(x)) << 32) | quint32(y);
}
struct Prefix {
    QHash<int, int> next;
    int fail = 0;
    bool forbidden = false;
};
double bearingDelta(double before, double after) {
    double value = std::fmod(after - before + 540.0, 360.0) - 180.0;
    return value;
}
bool allowed(int type, const QString &mode) {
    if (mode == "car")
        return type >= 1 && type <= 0x0c;
    if (mode == "bicycle")
        return (type >= 3 && type <= 0x0c) || type == 0x16;
    return (type >= 3 && type <= 0x0c) || type == 0x16;
}
double speed(int type, const QString &mode) {
    if (mode == "walk")
        return 5.0;
    if (mode == "bicycle")
        return type == 0x16 ? 10.0 : 16.0;
    if (type <= 2)
        return 80.0;
    if (type <= 4)
        return 50.0;
    if (type <= 6)
        return 35.0;
    return 20.0;
}
} // namespace

QJsonObject planGarminRoute(const QJsonArray &ways, const QGeoCoordinate &start, const QGeoCoordinate &finish,
                            const QString &mode, const QString &strategy, const QJsonArray &restrictions,
                            bool requireDecodedAccess) {
    auto fail = [](const QString &message) { return QJsonObject{{"error", message}}; };
    if (!start.isValid() || !finish.isValid())
        return fail("起终点经纬度无效。");
    if (mode != "car" && mode != "walk" && mode != "bicycle")
        return fail("不支持的出行方式。");
    QVector<Node> nodes;
    QHash<quint64, int> junctions;
    QStringList names;
    QStringList roadIds;
    QVector<bool> localRoad;
    int edgeTokens = 0;
    QHash<QString, int> tokenIndex;
    auto tokenFor = [&](int from, int to, int way) {
        const auto road = roadIds[way].isEmpty() ? QString("way%1").arg(way) : roadIds[way];
        const auto key = road + ":" + QString::number(from) + ":" + QString::number(to);
        if (!tokenIndex.contains(key))
            tokenIndex.insert(key, edgeTokens++);
        return tokenIndex.value(key);
    };
    const int vehicle = mode == "walk" ? 0x10 : mode == "bicycle" ? 0x20 : 1;
    int sharedNodes = 0;
    for (const auto &value : ways) {
        const auto way = value.toObject();
        const int type = way.value("type").toInt();
        const bool decoded = way.value("accessDecoded").toBool();
        const int access = way.value("accessMask").toInt();
        if (requireDecodedAccess && !decoded)
            continue;
        if (decoded ? bool(access & vehicle) || (mode == "car" && (access & 8)) : !allowed(type, mode))
            continue;
        const auto coordinates = way.value("coordinates").toArray();
        if (coordinates.size() < 2)
            continue;
        QSet<int> joins;
        for (const auto &join : way.value("junctions").toArray())
            joins.insert(join.toInt());
        const bool flags = way.value("hasJunctionFlags").toBool();
        const bool oneway = mode != "walk" && way.value("oneway").toBool();
        const int nameIndex = names.size();
        names.append(way.value("name").toString());
        roadIds.append(way.value("roadId").toString());
        localRoad.append(mode != "walk" && bool(access & 0x80));
        int previous = -1;
        for (int i = 0; i < coordinates.size(); ++i) {
            const auto pair = coordinates[i].toArray();
            if (pair.size() != 2)
                return fail("路网坐标损坏。");
            const QGeoCoordinate coordinate(pair[1].toDouble(), pair[0].toDouble());
            if (!coordinate.isValid())
                return fail("路网坐标无效。");
            // Road endpoints are implicit junctions; internal shape vertices
            // only connect when Garmin marks them as routing junctions.
            const bool shared = i == 0 || i == coordinates.size() - 1 || (flags && joins.contains(i));
            const quint64 key = coordinateKey(pair[0].toDouble(), pair[1].toDouble());
            int index;
            if (shared && junctions.contains(key))
                index = junctions.value(key);
            else {
                index = nodes.size();
                nodes.append(Node{coordinate, {}});
                if (shared) {
                    junctions.insert(key, index);
                    ++sharedNodes;
                }
            }
            if (previous >= 0 && index != previous) {
                const double distance = nodes[previous].coordinate.distanceTo(coordinate);
                const double seconds = distance / (speed(type, mode) / 3.6);
                nodes[previous].edges.append(
                    {index, nameIndex, distance, seconds, tokenFor(previous, index, nameIndex)});
                if (!oneway)
                    nodes[index].edges.append(
                        {previous, nameIndex, distance, seconds, tokenFor(index, previous, nameIndex)});
            }
            previous = index;
        }
    }
    if (nodes.isEmpty())
        return fail("地图中没有适合此出行方式的可解析道路。");
    auto snap = [&](const QGeoCoordinate &coordinate, double &distance) {
        int best = -1;
        distance = 1000.0;
        for (int i = 0; i < nodes.size(); ++i) {
            const double candidate = coordinate.distanceTo(nodes[i].coordinate);
            if (candidate <= distance) {
                distance = candidate;
                best = i;
            }
        }
        return best;
    };
    double startOffset, finishOffset;
    const int from = snap(start, startOffset), to = snap(finish, finishOffset);
    if (from < 0 || to < 0)
        return fail("起点或终点距离可用道路超过 1 公里，请选择道路附近的位置。");
    // No-through roads may provide endpoint access, but must not form a shortcut
    // between two unrestricted roads. Track entering/leaving the endpoint zone.
    QVector<bool> originZone(nodes.size(), false), destinationZone(nodes.size(), false);
    QVector<QVector<int>> localIncoming(nodes.size());
    for (int u = 0; u < nodes.size(); ++u)
        for (const auto &e : nodes[u].edges)
            if (localRoad[e.way])
                localIncoming[e.destination].append(u);
    QVector<int> zoneQueue{from};
    originZone[from] = true;
    for (int q = 0; q < zoneQueue.size(); ++q)
        for (const auto &e : nodes[zoneQueue[q]].edges)
            if (localRoad[e.way] && !originZone[e.destination]) {
                originZone[e.destination] = true;
                zoneQueue.append(e.destination);
            }
    zoneQueue = {to};
    destinationZone[to] = true;
    for (int q = 0; q < zoneQueue.size(); ++q)
        for (int u : localIncoming[zoneQueue[q]])
            if (!destinationZone[u]) {
                destinationZone[u] = true;
                zoneQueue.append(u);
            }

    QVector<Prefix> trie(1);
    int applied = 0;
    for (const auto &v : restrictions) {
        auto r = v.toObject();
        if (r.value("exceptMask").toInt() & vehicle)
            continue;
        auto points = r.value("nodes").toArray(), roads = r.value("roads").toArray();
        if (points.size() != roads.size() + 1 || roads.size() < 2)
            return fail("转向禁令数据损坏。");
        bool irrelevant = false;
        for (const auto &road : roads)
            if (!roadIds.contains(road.toString()))
                irrelevant = true;
        if (irrelevant)
            continue; // An inaccessible constituent road already blocks this movement.
        QVector<QVector<int>> patterns{QVector<int>{}};
        for (int segment = 0; segment < roads.size(); ++segment) {
            auto pa = points[segment].toArray(), pb = points[segment + 1].toArray();
            if (pa.size() != 2 || pb.size() != 2)
                return fail("转向禁令坐标损坏。");
            int first = junctions.value(coordinateKey(pa[0].toDouble(), pa[1].toDouble()), -1);
            int last = junctions.value(coordinateKey(pb[0].toDouble(), pb[1].toDouble()), -1);
            if (first < 0 || last < 0)
                return fail("转向禁令无法匹配道路节点，已停止规划以避免忽略禁令。");
            // Preserve every shape representation of the NOD arc, including
            // split/duplicate polylines and loops sharing one NET road identity.
            QVector<QVector<int>> pieces;
            QSet<int> visiting;
            QVector<int> piece;
            int visits = 0;
            bool overflow = false;
            std::function<void(int)> walk = [&](int u) {
                if (++visits > 20000 || pieces.size() > 64 || piece.size() > 2048) {
                    overflow = true;
                    return;
                }
                if (u == last) {
                    if (!pieces.contains(piece))
                        pieces.append(piece);
                    return;
                }
                visiting.insert(u);
                QSet<int> seenTokens;
                for (const auto &e : nodes[u].edges) {
                    if (roadIds[e.way] != roads[segment].toString() || visiting.contains(e.destination) ||
                        seenTokens.contains(e.token))
                        continue;
                    seenTokens.insert(e.token);
                    piece.append(e.token);
                    walk(e.destination);
                    piece.removeLast();
                    if (overflow)
                        break;
                }
                visiting.remove(u);
            };
            walk(first);
            if (overflow)
                return fail("转向禁令道路存在过多歧义连接，已停止规划以避免漏用禁令。");
            if (pieces.isEmpty()) {
                irrelevant = true;
                break;
            } // e.g. opposite of a one-way road
            QVector<QVector<int>> combined;
            for (const auto &prefix : patterns)
                for (const auto &suffix : pieces) {
                    combined.append(prefix + suffix);
                    if (combined.size() > 256)
                        return fail("转向禁令路径组合过多，无法安全规划。");
                }
            patterns = combined;
        }
        if (irrelevant)
            continue;
        for (const auto &pattern : patterns) {
            if (pattern.isEmpty())
                continue;
            int state = 0;
            for (int token : pattern) {
                if (!trie[state].next.contains(token)) {
                    int n = trie.size();
                    trie[state].next.insert(token, n);
                    trie.append(Prefix{});
                }
                state = trie[state].next[token];
            }
            trie[state].forbidden = true;
        }
        ++applied;
    }
    QVector<int> pending;
    for (int child : trie[0].next)
        pending.append(child);
    for (int q = 0; q < pending.size(); ++q) {
        int u = pending[q];
        const auto children = trie[u].next;
        for (auto it = children.begin(); it != children.end(); ++it) {
            int f = trie[u].fail;
            while (f && !trie[f].next.contains(it.key()))
                f = trie[f].fail;
            trie[it.value()].fail = trie[f].next.value(it.key(), 0);
            trie[it.value()].forbidden |= trie[trie[it.value()].fail].forbidden;
            pending.append(it.value());
        }
    }
    auto transition = [&](int state, int token) {
        while (state && !trie[state].next.contains(token))
            state = trie[state].fail;
        return trie[state].next.value(token, 0);
    };
    const double infinity = std::numeric_limits<double>::infinity();
    // State includes the incoming path prefix; node-only Dijkstra cannot enforce turns.
    using State = quint64;
    auto stateKey = [](int node, int prefix, int phase) {
        return (quint64(quint32(node)) << 32) | (quint64(prefix) << 2) | phase;
    };
    QHash<State, double> costs;
    QHash<State, State> parent;
    QHash<State, int> previousEdge;
    using Entry = std::pair<double, State>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    State initial = stateKey(from, 0, false), terminal = 0;
    bool found = false;
    costs[initial] = 0;
    queue.emplace(0, initial);
    while (!queue.empty()) {
        const auto [cost, state] = queue.top();
        queue.pop();
        int node = int(state >> 32), prefix = int(quint32(state) >> 2), phase = state & 3;
        if (cost > costs.value(state, infinity))
            continue;
        if (node == to) {
            terminal = state;
            found = true;
            break;
        }
        for (int i = 0; i < nodes[node].edges.size(); ++i) {
            const auto &edge = nodes[node].edges[i];
            bool local = localRoad[edge.way];
            int nextPhase = phase;
            if (local) {
                if (phase == 0 && originZone[node] && originZone[edge.destination])
                    nextPhase = 0;
                else if (destinationZone[node] && destinationZone[edge.destination])
                    nextPhase = 2;
                else
                    continue;
            } else {
                if (phase == 2)
                    continue;
                nextPhase = 1;
            }
            int nextPrefix = transition(prefix, edge.token);
            if (trie[nextPrefix].forbidden)
                continue;
            State nextState = stateKey(edge.destination, nextPrefix, nextPhase);
            const double next =
                cost + (strategy == "shortest" || mode != "car" ? edge.distance : edge.seconds);
            if (next < costs.value(nextState, infinity)) {
                costs[nextState] = next;
                parent[nextState] = state;
                previousEdge[nextState] = i;
                queue.emplace(next, nextState);
            }
        }
    }
    if (!found)
        return fail("没有找到连通路线：可能受单行方向、道路分区或尚未解析的连接数据影响。");
    QVector<int> path;
    QVector<int> pathEdges;
    for (State s = terminal;; s = parent[s]) {
        path.append(int(s >> 32));
        if (s == initial)
            break;
        pathEdges.append(previousEdge[s]);
    }
    std::reverse(path.begin(), path.end());
    std::reverse(pathEdges.begin(), pathEdges.end());
    if (path.size() < 2)
        return fail("起终点匹配到同一位置，请选择相距更远的地点。");
    QJsonArray coordinates, instructions, cumulative, routeRoadIds;
    double distance = 0, seconds = 0;
    QString previousName;
    double previousBearing = 0;
    for (int i = 0; i < path.size(); ++i) {
        const auto coordinate = nodes[path[i]].coordinate;
        coordinates.append(QJsonArray{coordinate.longitude(), coordinate.latitude()});
        cumulative.append(distance);
        if (i == path.size() - 1)
            break;
        const auto &edge = nodes[path[i]].edges[pathEdges[i]];
        routeRoadIds.append(roadIds[edge.way]);
        const QString name = names[edge.way];
        const double bearing = coordinate.azimuthTo(nodes[path[i + 1]].coordinate);
        const double turn = i == 0 ? 0 : bearingDelta(previousBearing, bearing);
        if (i == 0 || (std::abs(turn) > 35 && edge.distance > 3) ||
            (name != previousName && !name.isEmpty())) {
            QString maneuver = i == 0                 ? "depart"
                               : std::abs(turn) > 150 ? "uturn"
                               : turn > 35            ? "right"
                               : turn < -35           ? "left"
                                                      : "continue";
            const QString verb = maneuver == "depart"  ? "出发"
                                 : maneuver == "right" ? "右转"
                                 : maneuver == "left"  ? "左转"
                                 : maneuver == "uturn" ? "掉头"
                                                       : "继续直行";
            instructions.append(QJsonObject{{"index", i},
                                            {"distanceAlong", distance},
                                            {"maneuver", maneuver},
                                            {"text", verb + (name.isEmpty() ? QString() : "，沿 " + name)}});
        }
        distance += edge.distance;
        seconds += edge.seconds;
        previousName = name;
        previousBearing = bearing;
    }
    instructions.append(QJsonObject{{"index", path.size() - 1},
                                    {"distanceAlong", distance},
                                    {"maneuver", "arrive"},
                                    {"text", "到达终点"}});
    return {{"coordinates", coordinates},
            {"roadIds", routeRoadIds},
            {"instructions", instructions},
            {"cumulative", cumulative},
            {"distance", distance},
            {"seconds", seconds},
            {"startOffset", startOffset},
            {"finishOffset", finishOffset},
            {"mode", mode},
            {"strategy", strategy},
            {"graphNodes", nodes.size()},
            {"junctionNodes", sharedNodes},
            {"appliedRestrictions", applied},
            {"limitations", requireDecodedAccess
                                ? "已执行车辆准入、单行及转向禁令；此图未发现已识别的时段条件，未知条件编码会"
                                  "拒绝规划。时间按道路类别估算。"
                                : "旧版路网缺少完整限制字段，请重新加载 IMG。时间按道路类别估算。"}};
}
