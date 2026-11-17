import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    objectName: "conversionDialog"
    required property var converter
    signal openMap(url file)
    title: qsTr("转换 OSM 地图")
    width: Math.min(760, parent ? parent.width - 24 : 760)
    height: Math.min(720, parent ? parent.height - 24 : 720)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    modal: false
    closePolicy: Popup.CloseOnEscape
    property string outputFormat: formatBox.currentValue || "mbtiles"

    function suggestOutput() {
        if (sourcePath.text.length)
            outputPath.text = converter.localPath(converter.suggestedOutput(
                converter.fileUrl(sourcePath.text), outputFormat))
    }
    function selectedElements() {
        let result = []
        for (let i = 0; i < elementRepeater.count; ++i) {
            const item = elementRepeater.itemAt(i)
            if (item.checked) result.push(item.modelData.key)
        }
        return result
    }
    FileDialog {
        id: sourceDialog
        title: qsTr("选择 OSM 地图")
        nameFilters: [qsTr("OSM 地图 (*.osm.pbf)")]
        onAccepted: {
            sourcePath.text = converter.localPath(selectedFile)
            dialog.suggestOutput()
        }
    }
    FileDialog {
        id: outputDialog
        title: qsTr("保存转换结果（请选择新文件名）")
        fileMode: FileDialog.SaveFile
        nameFilters: dialog.outputFormat === "pmtiles" ? ["PMTiles (*.pmtiles)"] : ["MBTiles (*.mbtiles)"]
        defaultSuffix: dialog.outputFormat
        onAccepted: outputPath.text = converter.localPath(selectedFile)
    }
    FolderDialog {
        id: outputFolderDialog
        title: qsTr("选择导出父目录，将在其中新建瓦片目录")
        onAccepted: {
            const suggested = converter.localPath(converter.suggestedOutput(
                converter.fileUrl(sourcePath.text), "xyz"))
            outputPath.text = converter.localPath(selectedFolder) + "/" + suggested.split("/").pop()
        }
    }
    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("将 OSM 原始地图转换为可离线使用的矢量瓦片。")
                wrapMode: Text.WordWrap
            }
            Label {
                visible: !converter.available
                Layout.fillWidth: true
                text: qsTr("当前安装未包含转换引擎，请安装启用 tilemaker 的完整版本。")
                color: "#b54708"
                wrapMode: Text.WordWrap
            }
            ColumnLayout {
                Layout.fillWidth: true
                enabled: !converter.running
                spacing: 10
                Label { text: qsTr("源地图") }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: sourcePath
                        objectName: "conversionSource"
                        Layout.fillWidth: true
                        placeholderText: qsTr("选择 .osm.pbf 文件")
                        onEditingFinished: if (!outputPath.text.length) dialog.suggestOutput()
                    }
                    Button { text: qsTr("浏览…"); onClicked: sourceDialog.open() }
                }
                Label { text: qsTr("保留的地图要素") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: dialog.width < 540 ? 1 : 2
                    columnSpacing: 24
                    Repeater {
                        id: elementRepeater
                        objectName: "conversionElements"
                        model: converter.elements
                        CheckBox {
                            required property var modelData
                            objectName: "element_" + modelData.key
                            text: modelData.label
                            checked: true
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("显示风格") }
                    ComboBox {
                        id: styleBox
                        objectName: "conversionStyle"
                        Layout.fillWidth: true
                        textRole: "label"; valueRole: "key"
                        model: [{key: "standard", label: qsTr("标准")},
                            {key: "light", label: qsTr("浅色")}, {key: "dark", label: qsTr("深色")}]
                    }
                    Label { text: qsTr("输出格式") }
                    ComboBox {
                        id: formatBox
                        objectName: "conversionFormat"
                        Layout.fillWidth: true
                        textRole: "label"; valueRole: "key"
                        model: [{key: "mbtiles", label: "MBTiles"}, {key: "pmtiles", label: "PMTiles"},
                            {key: "xyz", label: qsTr("XYZ 瓦片目录")}]
                        onActivated: dialog.suggestOutput()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: dialog.outputFormat === "mbtiles"
                        ? qsTr("MBTiles 可在转换后直接打开；所选风格会自动应用，并另存为配套样式文件。")
                        : qsTr("此格式支持导出，并附带所选风格的样式文件；当前 Viewer 暂不支持直接打开。")
                }
                RowLayout {
                    Label { text: qsTr("瓦片级别") }
                    SpinBox { id: minZoom; objectName: "conversionMinZoom"; from: 0; to: 14; value: 0; editable: true }
                    Label { text: "—" }
                    SpinBox { id: maxZoom; objectName: "conversionMaxZoom"; from: 0; to: 14; value: 14; editable: true }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("通常保留 0–14；Viewer 可继续放大显示。仅使用源文件中的 OSM 要素，不附加全球海岸线数据。")
                }
                Label { text: dialog.outputFormat === "xyz" ? qsTr("新建瓦片目录") : qsTr("导出文件") }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: outputPath
                        objectName: "conversionOutput"
                        Layout.fillWidth: true
                        placeholderText: qsTr("请选择尚不存在的输出路径")
                    }
                    Button {
                        text: qsTr("浏览…")
                        onClicked: {
                            if (dialog.outputFormat === "xyz") outputFolderDialog.open()
                            else {
                                outputDialog.selectedFile = converter.fileUrl(outputPath.text)
                                outputDialog.open()
                            }
                        }
                    }
                }
            }
            ProgressBar { Layout.fillWidth: true; visible: converter.running; indeterminate: true }
            Label {
                objectName: "conversionStatus"
                Layout.fillWidth: true
                text: converter.status
                wrapMode: Text.WrapAnywhere
                visible: text.length > 0
            }
            CheckBox { id: showLog; text: qsTr("显示运行日志") }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                visible: showLog.checked
                clip: true
                TextArea {
                    text: converter.log
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    onTextChanged: cursorPosition = length
                }
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "conversionStart"
            text: qsTr("开始转换")
            enabled: converter.available && !converter.running && sourcePath.text.length > 0 && outputPath.text.length > 0
            onClicked: converter.start(converter.fileUrl(sourcePath.text), converter.fileUrl(outputPath.text),
                dialog.selectedElements(), styleBox.currentValue, dialog.outputFormat, minZoom.value, maxZoom.value)
        }
        Button { text: qsTr("取消转换"); visible: converter.running; onClicked: converter.cancel() }
        Button {
            objectName: "conversionOpen"
            text: qsTr("打开导出的地图")
            visible: converter.canOpen && !converter.running
            onClicked: dialog.openMap(converter.result)
        }
        Button {
            text: converter.running ? qsTr("后台运行") : qsTr("关闭")
            onClicked: dialog.close()
        }
    }
}
