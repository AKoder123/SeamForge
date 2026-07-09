#include "AppState.h"

#include "core/Io.h"

#include <algorithm>

namespace {
constexpr size_t kMaxUndo = 64;
}

std::string AppState::snapshot() const {
    sf::Project p;
    p.sourcePath = sourcePath.toStdString();
    p.mesh = mesh;
    p.seams = seams;
    p.panels = panels;
    p.relations = relations;
    p.regularized = regularized;
    p.flattenerName = flattenerName.toStdString();
    p.validationText = validation.toText();
    return p.toJson().dump();
}

void AppState::restore(const std::string& snap) {
    std::string err;
    sf::Project p = sf::Project::fromJson(nlohmann::json::parse(snap), &err);
    if (!err.empty()) return;
    sourcePath = QString::fromStdString(p.sourcePath);
    mesh = std::move(p.mesh);
    seams = std::move(p.seams);
    panels = std::move(p.panels);
    relations = std::move(p.relations);
    regularized = std::move(p.regularized);
    flattenerName = QString::fromStdString(p.flattenerName);
    hasMesh = !mesh.F.empty();
    if (hasMesh) curvature = sf::computeCurvature(mesh);
    emit changed();
}

void AppState::pushUndo() {
    if (!hasMesh) return;
    undoStack_.push_back(snapshot());
    if (undoStack_.size() > kMaxUndo) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

void AppState::undo() {
    if (undoStack_.empty()) return;
    redoStack_.push_back(snapshot());
    restore(undoStack_.back());
    undoStack_.pop_back();
}

void AppState::redo() {
    if (redoStack_.empty()) return;
    undoStack_.push_back(snapshot());
    restore(redoStack_.back());
    redoStack_.pop_back();
}

bool AppState::importMesh(const QString& path, QString* err) {
    auto importer = sf::makeImporterFor(path.toStdString());
    auto res = importer->load(path.toStdString());
    if (!res.success) {
        if (err) *err = QString::fromStdString(res.message);
        return false;
    }
    pushUndo();
    mesh = std::move(res.mesh);
    validation = sf::validateAndRepair(mesh);
    curvature = sf::computeCurvature(mesh);
    seams.clear();
    panels.clear();
    relations.clear();
    regularized.clear();
    sourcePath = path;
    hasMesh = true;
    emit logMessage(QString::fromStdString(validation.toText()));
    if (!validation.flattenable())
        emit logMessage("mesh has blocking validation errors - fix before reconstruction");
    emit changed();
    return true;
}

bool AppState::openProject(const QString& path, QString* err) {
    sf::Project p;
    std::string e;
    if (!sf::Project::load(path.toStdString(), p, &e)) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    pushUndo();
    sourcePath = QString::fromStdString(p.sourcePath);
    mesh = std::move(p.mesh);
    seams = std::move(p.seams);
    panels = std::move(p.panels);
    relations = std::move(p.relations);
    regularized = std::move(p.regularized);
    if (!p.flattenerName.empty()) flattenerName = QString::fromStdString(p.flattenerName);
    hasMesh = !mesh.F.empty();
    if (hasMesh) curvature = sf::computeCurvature(mesh);
    validation = {};
    emit logMessage("project loaded: " + QString::number(panels.size()) + " panels, " +
                    QString::number(seams.size()) + " seams");
    emit changed();
    return true;
}

bool AppState::saveProject(const QString& path, QString* err) {
    sf::Project p;
    p.sourcePath = sourcePath.toStdString();
    p.mesh = mesh;
    p.seams = seams;
    p.panels = panels;
    p.relations = relations;
    p.regularized = regularized;
    p.flattenerName = flattenerName.toStdString();
    p.validationText = validation.toText();
    std::string e;
    if (!p.save(path.toStdString(), &e)) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    emit logMessage("project saved -> " + path);
    return true;
}

int AppState::nextSeamId() const {
    int id = 0;
    for (const auto& s : seams) id = std::max(id, s.id + 1);
    return id;
}

int AppState::addSeam(std::vector<int> vertices, sf::Seam::Source source, double confidence) {
    sf::Seam s;
    s.id = nextSeamId();
    s.vertices = std::move(vertices);
    s.source = source;
    s.confidence = confidence;
    std::string verr = sf::validateSeamPath(mesh, s);
    if (!verr.empty()) {
        emit logMessage("seam rejected: " + QString::fromStdString(verr));
        return -1;
    }
    pushUndo();
    seams.push_back(std::move(s));
    emit changed();
    return seams.back().id;
}

bool AppState::deleteSeam(int seamId) {
    auto it = std::find_if(seams.begin(), seams.end(),
                           [&](const sf::Seam& s) { return s.id == seamId; });
    if (it == seams.end()) return false;
    pushUndo();
    seams.erase(it);
    emit changed();
    return true;
}

void AppState::proposeSeams(QString* log) {
    auto prop = sf::proposeSideSeams(mesh);
    if (log) *log = QString::fromStdString(prop.log);
    if (prop.seams.empty()) {
        emit logMessage("proposal failed:\n" + QString::fromStdString(prop.log));
        return;
    }
    pushUndo();
    for (auto& s : prop.seams) {
        s.id = nextSeamId();
        seams.push_back(std::move(s));
    }
    emit logMessage(QString::fromStdString(prop.log));
    emit changed();
}

bool AppState::segment(QString* err) {
    auto seg = sf::segmentBySeams(mesh, seams);
    for (const auto& p : seg.problems)
        emit logMessage("segmentation: " + QString::fromStdString(p));
    if (seg.panels.size() < 2) {
        if (err) *err = "segmentation did not produce multiple panels";
        return false;
    }
    pushUndo();
    panels = std::move(seg.panels);
    // heuristic front/back labels for the two-panel case (user-correctable)
    if (panels.size() == 2) {
        auto meanZ = [](const sf::Panel& p) {
            double z = 0;
            for (const auto& v : p.V) z += v.z();
            return z / (double)p.V.size();
        };
        int front = meanZ(panels[0]) > meanZ(panels[1]) ? 0 : 1;
        panels[front].label = "front";
        panels[1 - front].label = "back";
    }
    relations = sf::deriveSeamRelations(mesh, seams, {panels, {}});
    regularized.clear();
    emit logMessage(QString::number(panels.size()) + " panels extracted");
    emit changed();
    return true;
}

bool AppState::flatten(QString* err) {
    if (panels.empty()) {
        if (err) *err = "segment the garment first";
        return false;
    }
    auto flattener = sf::makeFlattener(flattenerName.toStdString());
    if (!flattener) {
        if (err) *err = "unknown flattener " + flattenerName;
        return false;
    }
    pushUndo();
    for (auto& p : panels) {
        auto fr = flattener->flatten(p);
        if (!fr.success) {
            if (err) *err = QString("panel %1: %2").arg(p.id).arg(
                QString::fromStdString(fr.message));
            emit logMessage("flatten FAILED on panel " + QString::number(p.id) + ": " +
                            QString::fromStdString(fr.message));
            return false;
        }
        p.UV = fr.UV;
        auto ds = sf::summarizeDistortion(p, p.UV, sf::computeDistortion(p, p.UV));
        emit logMessage(QString("panel %1 (%2): flipped=%3, mean angle=%4, mean |log area|=%5, boundary %6%")
                            .arg(p.id)
                            .arg(QString::fromStdString(p.label))
                            .arg(ds.flipped)
                            .arg(ds.meanAngleDistortion, 0, 'f', 4)
                            .arg(ds.meanAbsLogArea, 0, 'f', 4)
                            .arg(ds.boundaryLengthChange * 100, 0, 'f', 2));
    }
    sf::updateRelationLengths(relations, panels);
    // regularise
    regularized.clear();
    sf::RegularizeOptions ro;
    ro.tolerance = 0.004 * mesh.bbox().diagonal().norm();
    for (const auto& p : panels) {
        std::vector<sf::RegularizedLoop> per;
        sf::TriMesh pm = p.toTriMesh();
        for (const auto& loop : pm.boundaryLoops()) {
            std::vector<sf::Vec2> pts;
            for (int v : loop) pts.push_back(p.UV[v]);
            auto reg = sf::regularizeLoop(pts, ro);
            sf::CurveFitOptions cfo;
            cfo.tolerance = ro.tolerance;
            sf::fitLoopCurves(reg, cfo);
            emit logMessage(QString("panel %1 boundary: %2 curve segments, fit dev %3, length err %4%")
                                .arg(p.id)
                                .arg(reg.curves.size())
                                .arg(reg.curveMaxDeviation, 0, 'g', 3)
                                .arg(reg.curveMaxLengthError * 100, 0, 'g', 3));
            per.push_back(std::move(reg));
        }
        regularized.push_back(std::move(per));
    }
    emit changed();
    return true;
}

void AppState::setPanelLabel(int panelId, const QString& label) {
    for (auto& p : panels)
        if (p.id == panelId) {
            pushUndo();
            p.label = label.toStdString();
            emit changed();
            return;
        }
}

void AppState::toggleRelationReversed(int seamId) {
    for (auto& r : relations)
        if (r.seamId == seamId) {
            pushUndo();
            r.reversed = !r.reversed;
            emit logMessage(QString("seam %1 sewing direction %2")
                                .arg(seamId)
                                .arg(r.reversed ? "reversed" : "normal"));
            emit changed();
            return;
        }
}
