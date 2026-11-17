#include "stylepalette.h"
#include "runtimepaths.h"
#include <QJsonArray>
#include <QHash>

QJsonObject applyStylePalette(QJsonObject style, const QString &preset) {
    const QHash<QString, QString> dark{
        {"#f3f0e8", "#202630"}, {"#dbe8c6", "#33483b"}, {"#e2e5d3", "#303b38"},
        {"#bbd8ac", "#365442"}, {"#9ac9df", "#294f69"}, {"#d5c9bb", "#535a65"},
        {"#c4b5a5", "#68717e"}, {"#c8c0b4", "#303640"}, {"#ffffff", "#85909e"},
        {"#f3ca76", "#d1ab62"}, {"#9b9390", "#a7a5a1"}, {"#a193b5", "#a693bd"},
        {"#414954", "#e4e9ef"}};
    const QHash<QString, QString> light{
        {"#f3f0e8", "#fafafa"}, {"#dbe8c6", "#e5eddf"}, {"#e2e5d3", "#ededeb"},
        {"#bbd8ac", "#d7e7cf"}, {"#9ac9df", "#b9dce8"}, {"#d5c9bb", "#dedede"},
        {"#c4b5a5", "#c9c9c9"}, {"#c8c0b4", "#d2d2d2"}, {"#f3ca76", "#e9d6a8"}};
    const auto palette = preset == "dark" ? dark : preset == "light" ? light : QHash<QString, QString>{};
    auto layers = style.value("layers").toArray();
    for (int i = 0; i < layers.size(); ++i) {
        auto layer = layers[i].toObject();
        auto paint = layer.value("paint").toObject();
        for (auto it = paint.begin(); it != paint.end(); ++it)
            if (it.value().isString() && palette.contains(it.value().toString()))
                it.value() = palette.value(it.value().toString());
        if (preset == "dark" && paint.contains("text-halo-color"))
            paint.insert("text-halo-color", "#202630");
        layer.insert("paint", paint);
        layers[i] = layer;
    }
    style.insert("layers", layers);
    const auto glyphs = RuntimePaths::glyphsUrl();
    if (!glyphs.isEmpty()) style.insert("glyphs", glyphs);
    else {
        // Builds without bundled fonts still display geometry and POI markers.
        QJsonArray geometry;
        for (const auto &layer : layers)
            if (layer.toObject().value("type") != "symbol") geometry.append(layer);
        style.insert("layers", geometry);
    }
    return style;
}
