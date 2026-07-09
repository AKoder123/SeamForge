#pragma once
// Central document state for the GUI with snapshot-based undo/redo.
// Snapshots reuse the project JSON serialisation, so undo state and the
// on-disk format can never diverge.

#include "core/Curvature.h"
#include "core/Flatten.h"
#include "core/Project.h"
#include "core/Regularize.h"
#include "core/Relations.h"
#include "core/Seam.h"
#include "core/Segmentation.h"
#include "core/Validation.h"

#include <QObject>
#include <QString>
#include <vector>

class AppState : public QObject {
    Q_OBJECT
public:
    explicit AppState(QObject* parent = nullptr) : QObject(parent) {}

    sf::TriMesh mesh;
    sf::ValidationReport validation;
    sf::CurvatureField curvature;
    std::vector<sf::Seam> seams;
    std::vector<sf::Panel> panels;
    std::vector<sf::SeamRelation> relations;
    std::vector<std::vector<sf::RegularizedLoop>> regularized;
    QString sourcePath;
    QString flattenerName = "arap";
    bool hasMesh = false;

    // --- operations (each pushes an undo snapshot and emits changed) ---
    bool importMesh(const QString& path, QString* err);
    bool openProject(const QString& path, QString* err);
    bool saveProject(const QString& path, QString* err);
    int addSeam(std::vector<int> vertices, sf::Seam::Source source, double confidence);
    bool deleteSeam(int seamId);
    void proposeSeams(QString* log);
    // pre-cut garments: segment disconnected components (if not yet
    // segmented) and match panel boundaries into seam proposals
    bool matchBoundaries(QString* err);
    bool segment(QString* err);              // cut + label + relations
    bool flatten(QString* err);              // flatten all panels + regularise
    void setPanelLabel(int panelId, const QString& label);
    void toggleRelationReversed(int seamId);

    void undo();
    void redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    int nextSeamId() const;

signals:
    void changed();
    void logMessage(const QString& msg);

private:
    void pushUndo();
    std::string snapshot() const;
    void restore(const std::string& snap);
    std::vector<std::string> undoStack_, redoStack_;
};
