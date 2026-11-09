#include "network.h"
#include <QJsonArray>
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char *message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static void put(QByteArray &b, int p, int v, int n = 1) {
    for (int i = 0; i < n; ++i)
        b[p + i] = char(unsigned(v) >> (8 * i));
}
int main() {
    QByteArray b(96, 0);
    for (int n : {0, 6, 12}) {
        put(b, n + 1, 0x20);
        put(b, n + 2, n * 10, 2);
    }
    put(b, 64, 1);
    put(b, 65, 5400000, 3);
    put(b, 68, 1800000, 3);
    put(b, 71, 2);
    put(b, 73, 100 | 0xc00000, 3);
    put(b, 76, 0xab);
    put(b, 77, 0x31);
    put(b, 78, 200, 3);
    put(b, 81, 0x12);
    put(b, 83, 12);
    put(b, 84, 5);
    put(b, 85, 0x4a);
    put(b, 87, 0x21);
    put(b, 88, 0x8040, 2);
    put(b, 90, 0x803a, 2);
    put(b, 92, 0x8034, 2);
    put(b, 94, 0);
    put(b, 95, 1);
    auto parsed = decodeGarminNodRules(b, "test", 6);
    check(parsed["report"].toObject()["complete"].toBool(), "valid Table A/C fixture");
    auto road = parsed["roads"].toObject()["100"].toObject();
    check(road["accessMask"].toInt() == 0xc031, "delivery/emergency access bits in NET pointer");
    check(road["oneway"].toBool() && road["toll"].toBool() && road["speedClass"].toInt() == 3,
          "class/speed/oneway/toll");
    auto r = parsed["restrictions"].toArray().first().toObject();
    check(r["exceptMask"].toInt() == 0x31, "foot/car/bicycle exceptions");
    check(r["roads"].toArray() == QJsonArray{"test:100", "test:200"}, "restriction NET identities");
    auto points = r["nodes"].toArray();
    check(points.size() == 3 && points[0].toArray()[0].toDouble() < points[1].toArray()[0].toDouble(),
          "relative node coordinate decoding");
    auto bad = b;
    bad[84] = 7;
    check(!decodeGarminNodRules(bad, "test", 6)["report"].toObject()["complete"].toBool(),
          "unknown conditional type must fail closed");
    bad = b;
    bad[86] = 1;
    check(!decodeGarminNodRules(bad, "test", 6)["report"].toObject()["complete"].toBool(),
          "unknown conditional payload must fail closed");
    for (int size = 0; size < b.size(); ++size)
        check(!decodeGarminNodRules(b.left(size), "test", 6)["report"].toObject()["complete"].toBool(),
              "truncated NOD must not silently omit restrictions");
    bad = b;
    bad[95] = 2;
    check(!decodeGarminNodRules(bad, "test", 6)["report"].toObject()["complete"].toBool(),
          "out-of-range road reference");
    puts("PASS: binary NOD access/exception/coordinate rules, unknown conditions, every truncation boundary");
}
