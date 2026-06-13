#include "tools/calibration_tool.hpp"

#include "viewer/image_canvas.hpp"
#include "core/content_registry.hpp"

#include "imgui.h"

#include <cmath>
#include <cstring>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <vector>

namespace {

// ------------------------------------------------------------------
// Ported from algo/dab/pixel_calibration.py
// cKDTree replaced with brute-force O(N^2) nearest neighbor
// (corner count is typically < 1000, negligible cost)
// ------------------------------------------------------------------

struct Point2f {
    float x = 0, y = 0;
};

struct FilterResult {
    std::vector<Point2f> points;
    int xClusters = 0;
    int yClusters = 0;
    float commonGap = 0.0f;
};

struct CalibResult {
    bool success = false;
    std::vector<ImVec2> corners;
    float meanPixelDist = 0.0f;
    float mmPerPixel = 0.0f;
};

// --- mergeClosePoints (replaces cKDTree-based merge_close_points) ---

std::vector<Point2f> mergeClosePoints(const std::vector<Point2f>& pts, float threshold) {
    if (pts.empty()) return {};
    const int n = (int)pts.size();
    std::vector<bool> visited(n, false);
    std::vector<Point2f> result;

    for (int i = 0; i < n; ++i) {
        if (visited[i]) continue;
        float sx = pts[i].x, sy = pts[i].y;
        int count = 1;
        visited[i] = true;
        for (int j = i + 1; j < n; ++j) {
            if (visited[j]) continue;
            float dx = pts[j].x - pts[i].x;
            float dy = pts[j].y - pts[i].y;
            if (dx * dx + dy * dy <= threshold * threshold) {
                sx += pts[j].x;
                sy += pts[j].y;
                count++;
                visited[j] = true;
            }
        }
        result.push_back({sx / count, sy / count});
    }
    return result;
}

// --- removeEdgePoints ---

std::vector<Point2f> removeEdgePoints(const std::vector<Point2f>& pts, int imgH, int imgW, float edgeRatio) {
    float xl = imgW * edgeRatio;
    float xr = imgW * (1.0f - edgeRatio);
    float yt = imgH * edgeRatio;
    float yb = imgH * (1.0f - edgeRatio);

    std::vector<Point2f> out;
    for (const auto& p : pts) {
        if (p.x >= xl && p.x <= xr && p.y >= yt && p.y <= yb)
            out.push_back(p);
    }
    return out;
}

// --- filterValidChessboardPoints (main detection pipeline) ---

FilterResult filterValidChessboardPoints(const cv::Mat& gray) {
    FilterResult result;

    // Harris corner detection
    cv::Mat harris;
    cv::cornerHarris(gray, harris, 3, 5, 0.06);
    cv::dilate(harris, harris, cv::Mat());

    double maxVal = 0;
    cv::minMaxLoc(harris, nullptr, &maxVal);
    float thresh = (float)(0.01 * maxVal);

    // Extract corner coordinates (x, y)
    std::vector<Point2f> cornerCoords;
    for (int y = 0; y < harris.rows; ++y) {
        const float* row = harris.ptr<float>(y);
        for (int x = 0; x < harris.cols; ++x) {
            if (row[x] > thresh)
                cornerCoords.push_back({(float)x, (float)y});
        }
    }

    // Merge close points
    auto merged = mergeClosePoints(cornerCoords, 15.0f);
    // Remove edge points
    merged = removeEdgePoints(merged, gray.rows, gray.cols, 0.05f);
    if (merged.empty()) return result;

    // Filter by quadrant pattern: real chessboard corners have alternating
    // black/white quadrants, square centers do not.
    {
        const int halfWin = 5; // half window size for each quadrant
        std::vector<Point2f> filtered;
        for (const auto& p : merged) {
            int cx = (int)p.x, cy = (int)p.y;
            // Check bounds
            if (cx - halfWin < 0 || cx + halfWin >= gray.cols ||
                cy - halfWin < 0 || cy + halfWin >= gray.rows) {
                filtered.push_back(p); // keep if too close to edge to check
                continue;
            }
            // Compute mean of each quadrant
            float sumTL = 0, sumTR = 0, sumBL = 0, sumBR = 0;
            int count = 0;
            for (int dy = -halfWin; dy < 0; ++dy) {
                const uint8_t* row = gray.ptr<uint8_t>(cy + dy);
                for (int dx = -halfWin; dx < 0; ++dx) { sumTL += row[cx + dx]; count++; }
                for (int dx = 1; dx <= halfWin; ++dx) { sumTR += row[cx + dx]; }
            }
            for (int dy = 1; dy <= halfWin; ++dy) {
                const uint8_t* row = gray.ptr<uint8_t>(cy + dy);
                for (int dx = -halfWin; dx < 0; ++dx) { sumBL += row[cx + dx]; }
                for (int dx = 1; dx <= halfWin; ++dx) { sumBR += row[cx + dx]; }
            }
            float mTL = sumTL / count, mTR = sumTR / count;
            float mBL = sumBL / count, mBR = sumBR / count;

            // Chessboard corner: diagonal pairs alternate (TL≈BR, TR≈BL, but TL≠TR)
            // Pattern A: TL,BR are dark; TR,BL are bright
            // Pattern B: TL,BR are bright; TR,BL are dark
            float diagDiff1 = std::fabs((mTL + mBR) - (mTR + mBL)); // main diagonal contrast
            float diagDiff2 = std::fabs((mTL + mTR) - (mBL + mBR)); // horizontal contrast (should be small)
            float diagDiff3 = std::fabs((mTL + mBL) - (mTR + mBR)); // vertical contrast (should be small)

            // Keep if diagonal contrast is strong AND horizontal/vertical are weak
            // (true corners: high diagonal symmetry, not edge-like)
            if (diagDiff1 > 40.0f && diagDiff1 > diagDiff2 * 1.5f && diagDiff1 > diagDiff3 * 1.5f)
                filtered.push_back(p);
        }
        merged = std::move(filtered);
    }
    if (merged.empty()) return result;

    // Estimate commonGap from nearest-neighbor distances (median)
    float commonGap = 0.0f;
    if (merged.size() >= 2) {
        std::vector<float> nnDists;
        for (size_t i = 0; i < merged.size(); ++i) {
            float best = 1e9f;
            for (size_t j = 0; j < merged.size(); ++j) {
                if (i == j) continue;
                float dx = merged[i].x - merged[j].x;
                float dy = merged[i].y - merged[j].y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < best) best = d;
            }
            if (best < 1e9f) nnDists.push_back(best);
        }
        if (!nnDists.empty()) {
            std::sort(nnDists.begin(), nnDists.end());
            commonGap = nnDists[nnDists.size() / 2]; // median
        }
    }

    result.points = merged;
    result.xClusters = 0;
    result.yClusters = 0;
    result.commonGap = commonGap;
    return result;
}

// --- calibrateChessboard (full calibration pipeline) ---

CalibResult calibrateChessboard(const cv::Mat& bgrImg,
                                int roiX, int roiY, int roiW, int roiH,
                                float squareSizeMm)
{
    CalibResult result;

    // Validate ROI
    if (roiW <= 0 || roiH <= 0) return result;
    roiX = std::max(0, roiX);
    roiY = std::max(0, roiY);
    roiW = std::min(roiW, bgrImg.cols - roiX);
    roiH = std::min(roiH, bgrImg.rows - roiY);
    if (roiW <= 0 || roiH <= 0) return result;

    // Extract ROI and convert to grayscale
    cv::Mat roiBgr = bgrImg(cv::Rect(roiX, roiY, roiW, roiH)).clone();
    cv::Mat gray;
    cv::cvtColor(roiBgr, gray, cv::COLOR_BGR2GRAY);

    // --- Preprocessing ---
    // Bilateral filter + morphological open/close + Otsu binarization
    cv::Mat processed;
    {
        cv::Mat smoothed, opened, closed;
        cv::bilateralFilter(gray, smoothed, 9, 75, 75);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::morphologyEx(smoothed, opened, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1);
        cv::morphologyEx(opened, closed, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);
        cv::threshold(closed, processed, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }

    // Run detection pipeline on preprocessed image
    auto filtered = filterValidChessboardPoints(processed);
    if (filtered.points.empty()) return result;

    // Detected corners are already in ROI coordinates (no rotation/crop)
    std::vector<cv::Point2f> cornersInROI;
    cornersInROI.reserve(filtered.points.size());
    for (const auto& p : filtered.points)
        cornersInROI.push_back(cv::Point2f(p.x, p.y));

    // Subpixel refinement on ORIGINAL grayscale for accuracy
    cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.001);
    cv::cornerSubPix(gray, cornersInROI, cv::Size(5, 5), cv::Size(-1, -1), criteria);

    // Sort corners by Y then X for row grouping
    std::sort(cornersInROI.begin(), cornersInROI.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return (a.y < b.y) || (a.y == b.y && a.x < b.x);
    });

    // Group into rows (tolerance 4px)
    const float rowTol = 4.0f;
    std::vector<std::vector<cv::Point2f>> rows;
    {
        std::vector<cv::Point2f> currentRow = {cornersInROI[0]};
        float currentRowYMean = cornersInROI[0].y;
        for (size_t i = 1; i < cornersInROI.size(); ++i) {
            float y = cornersInROI[i].y;
            if (std::fabs(y - currentRowYMean) > rowTol) {
                std::sort(currentRow.begin(), currentRow.end(),
                          [](const cv::Point2f& a, const cv::Point2f& b) { return a.x < b.x; });
                rows.push_back(std::move(currentRow));
                currentRow = {cornersInROI[i]};
                currentRowYMean = y;
            } else {
                currentRow.push_back(cornersInROI[i]);
                currentRowYMean = (currentRowYMean * (currentRow.size() - 1) + y) / currentRow.size();
            }
        }
        if (!currentRow.empty()) {
            std::sort(currentRow.begin(), currentRow.end(),
                      [](const cv::Point2f& a, const cv::Point2f& b) { return a.x < b.x; });
            rows.push_back(std::move(currentRow));
        }
    }

    if (rows.size() < 2) return result;

    // Compute distances between adjacent corners
    std::vector<float> distances;
    // Horizontal
    for (const auto& row : rows) {
        for (size_t i = 1; i < row.size(); ++i) {
            float dx = row[i].x - row[i - 1].x;
            float dy = row[i].y - row[i - 1].y;
            distances.push_back(std::sqrt(dx * dx + dy * dy));
        }
    }
    // Vertical
    int minCols = (int)rows[0].size();
    for (const auto& row : rows)
        minCols = std::min(minCols, (int)row.size());
    for (int col = 0; col < minCols; ++col) {
        for (size_t row = 1; row < rows.size(); ++row) {
            float dx = rows[row][col].x - rows[row - 1][col].x;
            float dy = rows[row][col].y - rows[row - 1][col].y;
            distances.push_back(std::sqrt(dx * dx + dy * dy));
        }
    }

    // Filter outliers: keep distances within 15% of commonGap
    float tol = 0.15f * filtered.commonGap;
    std::vector<float> validDists;
    for (float d : distances) {
        if (std::fabs(d - filtered.commonGap) <= tol)
            validDists.push_back(d);
    }
    if (validDists.empty()) {
        validDists = distances;  // fallback to all
    }
    float meanDist = std::accumulate(validDists.begin(), validDists.end(), 0.0f) / (float)validDists.size();
    if (meanDist <= 0) return result;

    // Build result: cornersInROI are already in original ROI coordinates,
    // just add ROI offset to get full image coordinates
    result.success = true;
    result.meanPixelDist = meanDist;
    result.mmPerPixel = squareSizeMm / meanDist;
    for (const auto& c : cornersInROI)
        result.corners.push_back(ImVec2(c.x + (float)roiX, c.y + (float)roiY));

    return result;
}

// ------------------------------------------------------------------
// ImGui tool panel
// ------------------------------------------------------------------

void drawCalibrationTool(ImageCanvas& canvas) {
    bool enabled = canvas.calibMode;
    if (ImGui::Checkbox("启用校准", &enabled))
        canvas.setTool(enabled ? ImageCanvas::Tool::ChessboardCalib : ImageCanvas::Tool::None);

    ImGui::Separator();

    ImGui::SetNextItemWidth(100);
    if (ImGui::InputFloat("方块尺寸", &canvas.calibSquareSizeMm, 0.1f, 1.0f, "%.2f")) {
        if (canvas.calibRoiDone)
            canvas.calibNeedDetect = true; // re-detect with new square size
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(mm)");

    ImGui::Separator();

    // Auto-detect when ROI is freshly drawn (or square size changed)
    if (canvas.calibNeedDetect && canvas.calibRoiDone) {
        cv::Mat bgr = canvas.bgrImage();
        if (!bgr.empty() && canvas.calibRoiW > 0 && canvas.calibRoiH > 0) {
            CalibResult r = calibrateChessboard(
                bgr, canvas.calibRoiX, canvas.calibRoiY,
                canvas.calibRoiW, canvas.calibRoiH,
                canvas.calibSquareSizeMm);
            canvas.calibDetectionDone = true;
            canvas.calibDetectionOk = r.success;
            if (r.success) {
                canvas.calibCorners = std::move(r.corners);
                canvas.calibPixelDist = r.meanPixelDist;
                canvas.calibPixelToMm = r.mmPerPixel;
            } else {
                canvas.calibCorners.clear();
                canvas.calibPixelDist = 0.0f;
                canvas.calibPixelToMm = 0.0f;
            }
        }
        canvas.calibNeedDetect = false;
    }

    if (canvas.calibRoiDone) {
        ImGui::Text("ROI: %d x %d at (%d, %d)",
            canvas.calibRoiW, canvas.calibRoiH, canvas.calibRoiX, canvas.calibRoiY);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "在图像上拖拽选择棋盘格区域");
    }

    ImGui::Separator();

    // Results
    if (canvas.calibDetectionDone) {
        if (canvas.calibDetectionOk) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "检测成功");
            ImGui::Text("角点数: %d", (int)canvas.calibCorners.size());
            ImGui::Text("平均像素间距: %.2f px", canvas.calibPixelDist);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                "比例: %.6f mm/pixel", canvas.calibPixelToMm);

            ImGui::Separator();
            if (ImGui::Button("应用到测距工具")) {
                canvas.measureScale = canvas.calibPixelDist / canvas.calibSquareSizeMm; // pixels/mm
                std::strcpy(canvas.measureUnit, "mm");
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "检测失败，请调整 ROI 区域后重试");
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "未进行校准");
    }
}

} // anonymous namespace

void registerCalibrationTool(ImageCanvas& canvas) {
    hex::ContentRegistry::Tools::add("imziv.tool.calibration", "\xee\xaa\x99", [&canvas] {
        drawCalibrationTool(canvas);
    });
}
