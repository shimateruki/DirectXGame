#include "SceneValidator.h"

#include "BaseScene.h"
#include "CaptureToolWindow.h"
#include "DebugEditor.h"
#include "SceneManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {
using json = nlohmann::json;

// 監査結果を時系列で保存できるディレクトリ名を作る。
std::string MakeAuditTimestamp() {
  std::time_t now = std::time(nullptr);
  std::tm localTime{};
  localtime_s(&localTime, &now);
  std::ostringstream stream;
  stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
  return stream.str();
}

// 実行ディレクトリがproject直下でも、監査出力先をリポジトリ側へ揃える。
std::filesystem::path GetAuditRoot() {
  std::error_code ec;
  std::filesystem::path root = std::filesystem::current_path(ec);
  if (ec || root.empty()) {
    root = std::filesystem::path(L".");
  }
  if (root.filename() == L"project" && root.has_parent_path()) {
    root = root.parent_path();
  }
  return root / "captures" / "scene_audit";
}

std::string EscapeXml(const std::string &text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&apos;";
      break;
    default:
      escaped += c;
      break;
    }
  }
  return escaped;
}

std::array<Vector3, 8> GetObbCorners(const OBB &bounds) {
  const Vector3 axisX = bounds.orientations[0] * std::abs(bounds.size.x);
  const Vector3 axisY = bounds.orientations[1] * std::abs(bounds.size.y);
  const Vector3 axisZ = bounds.orientations[2] * std::abs(bounds.size.z);
  return {
      bounds.center - axisX - axisY - axisZ,
      bounds.center + axisX - axisY - axisZ,
      bounds.center - axisX + axisY - axisZ,
      bounds.center + axisX + axisY - axisZ,
      bounds.center - axisX - axisY + axisZ,
      bounds.center + axisX - axisY + axisZ,
      bounds.center - axisX + axisY + axisZ,
      bounds.center + axisX + axisY + axisZ,
  };
}

json VectorToJson(const Vector3 &value) {
  return json::array({value.x, value.y, value.z});
}

json ObbToJson(const OBB &bounds) {
  return {{"center", VectorToJson(bounds.center)},
          {"halfSize", VectorToJson(bounds.size)},
          {"axes", json::array({VectorToJson(bounds.orientations[0]),
                                VectorToJson(bounds.orientations[1]),
                                VectorToJson(bounds.orientations[2])})}};
}
} // namespace

bool SceneValidator::ExportVisualAudit() {
  if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
    auditStatusText_ = "監査対象のシーンがありません。";
    return false;
  }

  lastReportDirectory_ = GetAuditRoot() / MakeAuditTimestamp();
  std::error_code ec;
  std::filesystem::create_directories(lastReportDirectory_, ec);
  if (ec) {
    auditStatusText_ = "監査出力フォルダを作成できません: " + ec.message();
    return false;
  }

  const bool jsonSucceeded =
      WriteJsonReport(lastReportDirectory_ / "report.json");
  const bool svgSucceeded =
      WriteSvgOverview(lastReportDirectory_ / "overview.svg");
  bool captureSucceeded = false;
  if (editor_ && editor_->GetCaptureToolWindow()) {
    captureSucceeded = editor_->GetCaptureToolWindow()->CaptureGameViewToFile(
        lastReportDirectory_ / "game_view.png");
  }

  std::ostringstream status;
  status << (jsonSucceeded && svgSucceeded
                 ? "監査レポートを出力しました: "
                 : "監査レポートの一部出力に失敗しました: ")
         << lastReportDirectory_.generic_string();
  if (!captureSucceeded) {
    status << "（Game View画像は未保存）";
  }
  auditStatusText_ = status.str();
  return jsonSucceeded && svgSucceeded;
}

bool SceneValidator::CaptureSelectedIssue() {
  if (selectedIssueIndex_ < 0 ||
      selectedIssueIndex_ >= static_cast<int>(issues_.size()) || !editor_ ||
      !editor_->GetCaptureToolWindow()) {
    auditStatusText_ = "撮影する監査問題を選択してください。";
    return false;
  }

  if (lastReportDirectory_.empty()) {
    lastReportDirectory_ = GetAuditRoot() / MakeAuditTimestamp();
  }
  std::error_code ec;
  std::filesystem::create_directories(lastReportDirectory_, ec);
  if (ec) {
    auditStatusText_ = "撮影先フォルダを作成できません: " + ec.message();
    return false;
  }

  const std::filesystem::path output =
      lastReportDirectory_ /
      ("issue_" + std::to_string(selectedIssueIndex_ + 1) + ".png");
  const bool succeeded =
      editor_->GetCaptureToolWindow()->CaptureGameViewToFile(output);
  auditStatusText_ = succeeded
                         ? "選択問題を撮影しました: " + output.generic_string()
                         : "選択問題の撮影に失敗しました。Game "
                           "Viewが表示されているか確認してください。";
  return succeeded;
}

bool SceneValidator::WriteJsonReport(
    const std::filesystem::path &outputPath) const {
  json root;
  root["schemaVersion"] = 1;
  root["scene"] = sceneManager_ ? sceneManager_->GetCurrentSceneName() : "";
  root["generatedAt"] = MakeAuditTimestamp();
  root["settings"] = {
      {"floorPenetrationThreshold", floorPenetrationThreshold_},
      {"objectOverlapThreshold", objectOverlapThreshold_},
      {"dynamicOverlapThreshold", dynamicOverlapThreshold_},
      {"dynamicSampleCount", std::clamp(dynamicSampleCount_, 4, 32)}};
  root["issues"] = json::array();

  for (std::size_t index = 0; index < issues_.size(); ++index) {
    const Issue &issue = issues_[index];
    json entry = {{"index", index + 1},
                  {"severity", GetSeverityLabel(issue.severity)},
                  {"category", issue.category},
                  {"primaryObject", issue.objectName},
                  {"secondaryObject", issue.secondaryObjectName},
                  {"message", issue.message},
                  {"penetration", issue.penetration},
                  {"sampleTimeSeconds", issue.sampleTimeSeconds},
                  {"pathName", issue.pathName}};
    if (issue.hasPrimaryBounds)
      entry["primaryBounds"] = ObbToJson(issue.primaryBounds);
    if (issue.hasSecondaryBounds)
      entry["secondaryBounds"] = ObbToJson(issue.secondaryBounds);
    root["issues"].push_back(std::move(entry));
  }

  std::ofstream file(outputPath, std::ios::binary);
  if (!file)
    return false;
  file << root.dump(2);
  return file.good();
}

bool SceneValidator::WriteSvgOverview(
    const std::filesystem::path &outputPath) const {
  const std::vector<AuditCollider> colliders = CollectAuditColliders();
  if (colliders.empty()) {
    std::ofstream file(outputPath, std::ios::binary);
    if (!file)
      return false;
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"900\" "
            "height=\"240\">"
         << "<rect width=\"100%\" height=\"100%\" fill=\"#101722\"/>"
         << "<text x=\"32\" y=\"72\" fill=\"white\" font-size=\"28\">No "
            "auditable box colliders</text></svg>";
    return file.good();
  }

  const float largestValue = (std::numeric_limits<float>::max)();
  Vector3 minBounds = {largestValue, largestValue, largestValue};
  Vector3 maxBounds = {-largestValue, -largestValue, -largestValue};
  for (const AuditCollider &collider : colliders) {
    for (const Vector3 &corner : GetObbCorners(collider.bounds)) {
      minBounds.x = (std::min)(minBounds.x, corner.x);
      minBounds.y = (std::min)(minBounds.y, corner.y);
      minBounds.z = (std::min)(minBounds.z, corner.z);
      maxBounds.x = (std::max)(maxBounds.x, corner.x);
      maxBounds.y = (std::max)(maxBounds.y, corner.y);
      maxBounds.z = (std::max)(maxBounds.z, corner.z);
    }
  }

  constexpr float canvasWidth = 1500.0f;
  constexpr float canvasHeight = 900.0f;
  constexpr float panelWidth = 710.0f;
  constexpr float panelHeight = 700.0f;
  constexpr float panelTop = 145.0f;
  constexpr float firstPanelLeft = 28.0f;
  constexpr float secondPanelLeft = 762.0f;
  constexpr float padding = 34.0f;

  auto makeProjection = [&](bool topView, float panelLeft) {
    const float minHorizontal = topView ? minBounds.x : minBounds.x;
    const float maxHorizontal = topView ? maxBounds.x : maxBounds.x;
    const float minVertical = topView ? minBounds.z : minBounds.y;
    const float maxVertical = topView ? maxBounds.z : maxBounds.y;
    const float horizontalRange =
        (std::max)(1.0f, maxHorizontal - minHorizontal);
    const float verticalRange = (std::max)(1.0f, maxVertical - minVertical);
    const float scale =
        (std::min)((panelWidth - padding * 2.0f) / horizontalRange,
                   (panelHeight - padding * 2.0f) / verticalRange);
    return [=](const Vector3 &point) {
      const float horizontal = point.x;
      const float vertical = topView ? point.z : point.y;
      return Vector2{panelLeft + padding + (horizontal - minHorizontal) * scale,
                     panelTop + panelHeight - padding -
                         (vertical - minVertical) * scale};
    };
  };

  const auto topProjection = makeProjection(true, firstPanelLeft);
  const auto frontProjection = makeProjection(false, secondPanelLeft);
  static constexpr int edges[12][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 3},
                                       {1, 5}, {2, 3}, {2, 6}, {3, 7},
                                       {4, 5}, {4, 6}, {5, 7}, {6, 7}};

  std::ofstream file(outputPath, std::ios::binary);
  if (!file)
    return false;
  file << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1500\" "
          "height=\"900\" viewBox=\"0 0 1500 900\">\n";
  file << "<rect width=\"1500\" height=\"900\" fill=\"#0c1420\"/>\n";
  file << "<text x=\"28\" y=\"46\" fill=\"#ffffff\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"30\" font-weight=\"700\">Scene Visual "
          "Audit</text>\n";
  file << "<text x=\"28\" y=\"82\" fill=\"#91a6bc\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"18\">Scene: "
       << EscapeXml(sceneManager_ ? sceneManager_->GetCurrentSceneName() : "")
       << " / Issues: " << issues_.size() << "</text>\n";
  file << "<rect x=\"28\" y=\"145\" width=\"710\" height=\"700\" rx=\"10\" "
          "fill=\"#121f2e\" stroke=\"#29415a\"/>\n";
  file << "<rect x=\"762\" y=\"145\" width=\"710\" height=\"700\" rx=\"10\" "
          "fill=\"#121f2e\" stroke=\"#29415a\"/>\n";
  file << "<text x=\"48\" y=\"180\" fill=\"#dcecff\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"20\">Top (X/Z)</text>\n";
  file << "<text x=\"782\" y=\"180\" fill=\"#dcecff\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"20\">Front (X/Y)</text>\n";

  auto drawBounds = [&](const OBB &bounds, const char *color, float opacity,
                        float width) {
    const auto corners = GetObbCorners(bounds);
    for (const auto &projection : {topProjection, frontProjection}) {
      for (const auto &edge : edges) {
        const Vector2 first = projection(corners[edge[0]]);
        const Vector2 second = projection(corners[edge[1]]);
        file << "<line x1=\"" << first.x << "\" y1=\"" << first.y << "\" x2=\""
             << second.x << "\" y2=\"" << second.y << "\" stroke=\"" << color
             << "\" stroke-opacity=\"" << opacity << "\" stroke-width=\""
             << width << "\"/>\n";
      }
    }
  };

  for (const AuditCollider &collider : colliders) {
    drawBounds(collider.bounds, collider.isDynamic ? "#be8cff" : "#56708a",
               0.34f, 1.0f);
  }
  for (const Issue &issue : issues_) {
    if (issue.hasPrimaryBounds)
      drawBounds(issue.primaryBounds, "#ff4054", 0.92f, 2.5f);
    if (issue.hasSecondaryBounds)
      drawBounds(issue.secondaryBounds, "#20e6ff", 0.82f, 2.0f);
  }

  file << "<circle cx=\"1015\" cy=\"78\" r=\"7\" fill=\"#ff4054\"/><text "
          "x=\"1030\" y=\"84\" fill=\"#dcecff\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"16\">Primary</text>\n";
  file << "<circle cx=\"1135\" cy=\"78\" r=\"7\" fill=\"#20e6ff\"/><text "
          "x=\"1150\" y=\"84\" fill=\"#dcecff\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"16\">Secondary</text>\n";
  file << "<circle cx=\"1280\" cy=\"78\" r=\"7\" fill=\"#be8cff\"/><text "
          "x=\"1295\" y=\"84\" fill=\"#dcecff\" font-family=\"Segoe UI, "
          "sans-serif\" font-size=\"16\">Dynamic</text>\n";
  file << "</svg>\n";
  return file.good();
}
