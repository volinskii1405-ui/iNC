#pragma once

#include <QTimer>
#include <QWidget>

class QComboBox;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QSpinBox;
class QToolButton;

namespace mf {

class Document;

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(Document* doc, QWidget* parent = nullptr);

    // Actions created by the editor; the panel shows them as buttons.
    void setActions(QAction* add, QAction* dup, QAction* del, QAction* up, QAction* down, QAction* merge);

private:
    void rebuild();
    void syncProperties();
    void onItemChanged(QListWidgetItem* item);
    int rowToLayer(int row) const;

    Document* m_doc;
    QListWidget* m_list;
    QComboBox* m_mode;
    QSlider* m_opacity;
    QSpinBox* m_opacitySpin;
    QWidget* m_buttons;
    QTimer m_rebuildTimer;
    bool m_updating = false;
};

} // namespace mf
