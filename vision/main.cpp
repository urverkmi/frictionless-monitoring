#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <map>
#include <vector>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

// ============================================================
// USER PARAMETERS
// ============================================================

constexpr int    resolution_divider = 2;

constexpr int    DISPLAY_W          = 2028;
constexpr int    DISPLAY_H          = 1520;

constexpr double TAG_SIZE           = 0.056;   // metres, tracking tags
constexpr double CALIB_SQUARE       = 0.70;   // metres

constexpr int    TRACK_TAG0         = 0;
constexpr int    TRACK_TAG1         = 1;

// Confidence thresholds / weights  (tune to your scene)
constexpr double W_MARGIN           = 0.50;   // decision_margin weight
constexpr double W_HAMMING          = 0.25;   // hamming penalty weight
constexpr double W_REPROJ           = 0.15;   // reprojection-error weight
constexpr double W_SIZE             = 0.10;   // tag pixel-size weight

constexpr double MARGIN_SAT         = 80.0;   // margin value that maps to score 1.0
constexpr double SIZE_SAT           = 200.0;  // pixel diagonal that maps to score 1.0
constexpr double REPROJ_MAX         = 5.0;    // reprojection error (px) that maps to score 0.0
constexpr double MIN_TRACK_CONF      = 0.45;   // reject low-confidence pose outliers
constexpr double MAX_TRACK_JUMP_M    = 0.20;   // reject implausible frame-to-frame jumps

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool>     running(true);
std::atomic<bool>     calibrated(false);
std::atomic<uint64_t> frameCounter(0);

std::mutex  frameMutex;
cv::Mat     latestFrame;

// ============================================================
// DATA STRUCTS
// ============================================================

struct Pose
{
    double x   = 0.0;
    double y   = 0.0;
    double yaw = 0.0;   // degrees
};

struct TagState
{
    Pose   pose;
    double confidence = 0.0;   // [0 .. 1]
    bool   visible    = false;
    std::vector<cv::Point2f> corners;
};

static bool isPlausibleJump(const TagState& previous, const TagState& current)
{
    // If previous pose was not visible, accept reacquisition directly.
    if (!previous.visible) return true;
    const double dx = current.pose.x - previous.pose.x;
    const double dy = current.pose.y - previous.pose.y;
    const double jump = std::sqrt(dx * dx + dy * dy);
    return jump <= MAX_TRACK_JUMP_M;
}

std::mutex   poseMutex;
TagState     tag0State;
TagState     tag1State;

// UDP output for Python bridge (Detector Contract)
int udpSock = -1;
sockaddr_in udpAddr{};

// Camera -> world transform
cv::Mat R_wc;
cv::Mat t_wc;

// ============================================================
// CAMERA INTRINSICS  (full-res values, divided by resolution_divider)
// ============================================================

cv::Mat K = (cv::Mat1d(3, 3) <<
    4009.22661 / resolution_divider, 0.0,  2113.49677 / resolution_divider,
    0.0, 4020.48344 / resolution_divider,  1469.08894 / resolution_divider,
    0.0, 0.0, 1.0);

cv::Mat D = (cv::Mat1d(1, 5) <<
    -0.49, 0.28, 0.0, 0.0, -0.09);

// ============================================================
// WORLD CORNERS OF CALIBRATION TAGS
// ============================================================

std::map<int, std::vector<cv::Point3f>> worldTagCorners =
{
    {2, { {-CALIB_SQUARE/2,           -CALIB_SQUARE/2,           0},
          {-CALIB_SQUARE/2+TAG_SIZE,  -CALIB_SQUARE/2,           0},
          {-CALIB_SQUARE/2+TAG_SIZE,  -CALIB_SQUARE/2+TAG_SIZE,  0},
          {-CALIB_SQUARE/2,           -CALIB_SQUARE/2+TAG_SIZE,  0} }},

    {3, { { CALIB_SQUARE/2,           -CALIB_SQUARE/2,           0},
          { CALIB_SQUARE/2+TAG_SIZE,  -CALIB_SQUARE/2,           0},
          { CALIB_SQUARE/2+TAG_SIZE,  -CALIB_SQUARE/2+TAG_SIZE,  0},
          { CALIB_SQUARE/2,           -CALIB_SQUARE/2+TAG_SIZE,  0} }},

    {4, { { CALIB_SQUARE/2,           CALIB_SQUARE/2,            0},
          { CALIB_SQUARE/2+TAG_SIZE,   CALIB_SQUARE/2,            0},
          { CALIB_SQUARE/2+TAG_SIZE,   CALIB_SQUARE/2+TAG_SIZE,   0},
          { CALIB_SQUARE/2,            CALIB_SQUARE/2+TAG_SIZE,   0} }},

    {5, { {-CALIB_SQUARE/2,            CALIB_SQUARE/2,            0},
          {-CALIB_SQUARE/2+TAG_SIZE,   CALIB_SQUARE/2,            0},
          {-CALIB_SQUARE/2+TAG_SIZE,   CALIB_SQUARE/2+TAG_SIZE,   0},
          {-CALIB_SQUARE/2,            CALIB_SQUARE/2+TAG_SIZE,   0} }}
};

// ============================================================
// HELPERS
// ============================================================

// Clamp x to [0, 1]
static inline double clamp01(double x)
{
    return std::max(0.0, std::min(1.0, x));
}

// Pixel-space diagonal of the detected quad
static double tagPixelDiagonal(apriltag_detection_t* det)
{
    double dx = det->p[2][0] - det->p[0][0];
    double dy = det->p[2][1] - det->p[0][1];
    return std::sqrt(dx*dx + dy*dy);
}

// Reprojection error: re-project 3-D tag corners back with the solved pose,
// compare to detected 2-D corners.  Returns mean error in pixels.
static double reprojectionError(
    const std::vector<cv::Point3f>& obj,
    const std::vector<cv::Point2f>& img,
    const cv::Mat& rvec,
    const cv::Mat& tvec)
{
    std::vector<cv::Point2f> projected;
    cv::projectPoints(obj, rvec, tvec, K, D, projected);

    double err = 0.0;
    for (size_t i = 0; i < img.size(); ++i)
    {
        double dx = img[i].x - projected[i].x;
        double dy = img[i].y - projected[i].y;
        err += std::sqrt(dx*dx + dy*dy);
    }
    return err / static_cast<double>(img.size());
}

// ============================================================
// CONFIDENCE SCORE
//
// Combines four signals:
//
//   1. decision_margin  – how unambiguous the bit pattern match was.
//                         Higher = better.  Saturates at MARGIN_SAT.
//
//   2. hamming distance – number of bit-errors corrected.
//                         0 → score 1.0,  1 → 0.5,  2 → 0.0.
//                         (tag36h11 allows up to hamming=1 by default)
//
//   3. reprojection err – mean pixel error after solvePnP.
//                         0 px → 1.0,  REPROJ_MAX px → 0.0.
//
//   4. tag pixel size   – larger apparent size = more detail = better.
//                         Saturates at SIZE_SAT px diagonal.
//
// Final score is a weighted sum, clamped to [0, 1].
// ============================================================

static double computeConfidence(
    apriltag_detection_t* det,
    const cv::Mat& rvec,
    const cv::Mat& tvec,
    const std::vector<cv::Point3f>& obj,
    const std::vector<cv::Point2f>& img)
{
    // --- 1. decision_margin score ---
    double s_margin = clamp01(det->decision_margin / MARGIN_SAT);

    // --- 2. hamming score ---
    double s_hamming;
    switch (det->hamming)
    {
        case 0:  s_hamming = 1.00; break;
        case 1:  s_hamming = 0.50; break;
        default: s_hamming = 0.00; break;   // ≥2 errors → discard
    }

    // --- 3. reprojection error score ---
    double reproj   = reprojectionError(obj, img, rvec, tvec);
    double s_reproj = clamp01(1.0 - reproj / REPROJ_MAX);

    // --- 4. pixel size score ---
    double diag   = tagPixelDiagonal(det);
    double s_size = clamp01(diag / SIZE_SAT);

    // --- weighted sum ---
    double confidence =
        W_MARGIN  * s_margin  +
        W_HAMMING * s_hamming +
        W_REPROJ  * s_reproj  +
        W_SIZE    * s_size;

    return clamp01(confidence);
}

// ============================================================
// APRILTAG DETECTOR
// ============================================================

static apriltag_detector_t* createDetector()
{
    auto tf = tag36h11_create();
    auto td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate  = 2.0;
    td->nthreads       = 4;
    td->refine_edges   = 1;
    return td;
}

// ============================================================
// CAMERA CAPTURE THREAD
// ============================================================

void captureThread(GstElement* sink)
{
    while (running)
    {
        auto sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
        if (!sample) continue;

        auto buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        auto caps = gst_sample_get_caps(sample);
        auto s    = gst_caps_get_structure(caps, 0);

        int w, h;
        gst_structure_get_int(s, "width",  &w);
        gst_structure_get_int(s, "height", &h);

        cv::Mat frame(h, w, CV_8UC3, map.data);

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = frame.clone();
        }

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
    }
}

// ============================================================
// CAMERA -> WORLD
// ============================================================

static cv::Point3f camToWorld(const cv::Mat& tvec)
{
    if (R_wc.empty() || t_wc.empty()) return {0, 0, 0};
    cv::Mat p = R_wc * tvec + t_wc;
    return { (float)p.at<double>(0),
             (float)p.at<double>(1),
             (float)p.at<double>(2) };
}

// ============================================================
// CALIBRATION
// ============================================================

static bool calibrate(
    std::vector<cv::Point2f>& imgPts,
    std::vector<cv::Point3f>& objPts)
{
    if (imgPts.size() < 8) return false;

    cv::Mat rvec;
    cv::solvePnP(objPts, imgPts, K, D, rvec, t_wc);
    cv::Rodrigues(rvec, R_wc);
    calibrated = true;
    std::cerr << "[INFO] Calibration successful\n";
    return true;
}

// ============================================================
// JSON HELPERS
// ============================================================

// Fixed-precision double -> string (avoids locale issues with printf)
static std::string fp(double v, int prec = 4)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

static std::string tagJson(const std::string& key, const TagState& ts)
{
    return "\"" + key + "\":{"
        "\"x\":"          + fp(ts.pose.x)    +
        ",\"y\":"         + fp(ts.pose.y)     +
        ",\"yaw\":"       + fp(ts.pose.yaw)   +
        ",\"conf\":"      + fp(ts.confidence, 3) +
        ",\"visible\":"   + (ts.visible ? "true" : "false") +
        "}";
}

static bool initUdpSender(const std::string& host = "127.0.0.1", int port = 9001)
{
    udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSock < 0) return false;

    udpAddr = {};
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &udpAddr.sin_addr) != 1)
    {
        close(udpSock);
        udpSock = -1;
        return false;
    }
    return true;
}

static void sendDetectorContract(
    uint64_t frameId,
    double unixSeconds,
    const TagState& satellite,
    const TagState& endMass)
{
    if (udpSock < 0) return;

    // orbital angle from satellite -> end-mass vector (radians)
    const double dx = endMass.pose.x - satellite.pose.x;
    const double dy = endMass.pose.y - satellite.pose.y;
    const double orbital = std::atan2(dy, dx);
    const double conf = std::min(satellite.confidence, endMass.confidence);

    std::string payload =
        "{"
        "\"timestamp\":" + fp(unixSeconds, 6) +
        ",\"frame_id\":" + std::to_string(frameId) +
        ",\"camera_id\":\"cam0\"" +
        ",\"satellite_position\":{\"x\":" + fp(satellite.pose.x) + ",\"y\":" + fp(satellite.pose.y) + "}" +
        ",\"end_mass_position\":{\"x\":" + fp(endMass.pose.x) + ",\"y\":" + fp(endMass.pose.y) + "}" +
        ",\"orbital_angular_position\":" + fp(orbital, 6) +
        ",\"tracking_confidence\":" + fp(conf, 3) +
        "}";

    (void)sendto(
        udpSock,
        payload.c_str(),
        payload.size(),
        0,
        reinterpret_cast<const sockaddr*>(&udpAddr),
        sizeof(udpAddr));
}

// ============================================================
// TRACKING THREAD
// ============================================================

void trackingThread()
{
    auto detector = createDetector();

    // 3-D corners for a tracking tag (in tag-local frame, z=0)
    const std::vector<cv::Point3f> trackObj =
    {
        {-TAG_SIZE/2, -TAG_SIZE/2, 0},
        { TAG_SIZE/2, -TAG_SIZE/2, 0},
        { TAG_SIZE/2,  TAG_SIZE/2, 0},
        {-TAG_SIZE/2,  TAG_SIZE/2, 0}
    };

    while (running)
    {
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if (latestFrame.empty()) continue;
            frame = latestFrame.clone();
        }

        ++frameCounter;

        // --- Greyscale conversion ---
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        image_u8_t img =
        {
            gray.cols, gray.rows, gray.cols, gray.data
        };

        auto detections = apriltag_detector_detect(detector, &img);

        // Collect calibration correspondences
        std::vector<cv::Point2f> calibImg;
        std::vector<cv::Point3f> calibObj;

        // Reset visibility each frame
        TagState new0, new1;
        TagState prev0, prev1;
        {
            std::lock_guard<std::mutex> lock(poseMutex);
            new0 = tag0State;  new0.visible = false;
            new1 = tag1State;  new1.visible = false;
            new0.corners.clear();
            new1.corners.clear();
            prev0 = tag0State;
            prev1 = tag1State;
        }

        for (int i = 0; i < zarray_size(detections); ++i)
        {
            apriltag_detection_t* det;
            zarray_get(detections, i, &det);

            // Skip very low-quality detections early
            if (det->hamming > 1) continue;

            // --- Calibration tag corners ---
            if (worldTagCorners.count(det->id))
            {
                for (int k = 0; k < 4; ++k)
                {
                    calibImg.emplace_back(det->p[k][0], det->p[k][1]);
                    calibObj.push_back(worldTagCorners[det->id][k]);
                }
            }

            // --- Tracking tags ---
            if (calibrated &&
                (det->id == TRACK_TAG0 || det->id == TRACK_TAG1))
            {
                std::vector<cv::Point2f> imgPts;
                for (int k = 0; k < 4; ++k)
                    imgPts.emplace_back(det->p[k][0], det->p[k][1]);

                cv::Mat rvec, tvec;
                cv::solvePnP(trackObj, imgPts, K, D, rvec, tvec);

                // Confidence score (uses rvec/tvec from solvePnP)
                double conf = computeConfidence(det, rvec, tvec, trackObj, imgPts);

                // World position
                auto world = camToWorld(tvec);

                // Yaw from rotation matrix
                cv::Mat R;
                cv::Rodrigues(rvec, R);
                double yaw = std::atan2(R.at<double>(1, 0),
                                        R.at<double>(0, 0)) * 180.0 / CV_PI;

                TagState ts;
                ts.pose       = { world.x, world.y, yaw };
                ts.confidence = conf;
                ts.visible    = true;
                ts.corners    = imgPts;

                // Hard reject low-confidence detections: they are a major source
                // of repeated "fixed-value" spikes when a false tag pose appears.
                if (ts.confidence < MIN_TRACK_CONF) continue;

                if (det->id == TRACK_TAG0)
                {
                    if (isPlausibleJump(prev0, ts)) new0 = ts;
                }
                else
                {
                    if (isPlausibleJump(prev1, ts)) new1 = ts;
                }
            }
        }

        // Run calibration if not done yet
        if (!calibrated)
            calibrate(calibImg, calibObj);

        {
            std::lock_guard<std::mutex> lock(poseMutex);
            tag0State = new0;
            tag1State = new1;
        }

        apriltag_detections_destroy(detections);

        // --- JSON output (stderr stays clean for logs) ---
        auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        TagState s0, s1;
        {
            std::lock_guard<std::mutex> lock(poseMutex);
            s0 = tag0State;
            s1 = tag1State;
        }

        std::cout
            << "{"
            << "\"ts\":"    << ts_ns
            << ",\"frame\":" << frameCounter
            << ","           << tagJson("tag0", s0)
            << ","           << tagJson("tag1", s1)
            << "}\n";

        // Forward bridge contract when both tracking tags are visible.
        if (s0.visible && s1.visible)
        {
            auto now = std::chrono::system_clock::now();
            double unixSec = std::chrono::duration<double>(now.time_since_epoch()).count();
            sendDetectorContract(frameCounter.load(), unixSec, s0, s1);
        }
    }
}

// ============================================================
// VISUALIZATION THREAD
// ============================================================

void visThread()
{
    cv::namedWindow("Tracking", cv::WINDOW_NORMAL);
    cv::resizeWindow("Tracking", DISPLAY_W, DISPLAY_H);

    while (running)
    {
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if (latestFrame.empty()) continue;
            frame = latestFrame.clone();
        }

        const int srcW = frame.cols;
        const int srcH = frame.rows;
        cv::resize(frame, frame, cv::Size(DISPLAY_W, DISPLAY_H));

        TagState s0, s1;
        {
            std::lock_guard<std::mutex> lock(poseMutex);
            s0 = tag0State;
            s1 = tag1State;
        }

        auto drawTag = [&](const TagState& ts, const std::string& label,
                           int y, cv::Scalar colour)
        {
            std::string vis = ts.visible ? "" : " [lost]";
            std::string txt = label
                + " x=" + std::to_string(ts.pose.x).substr(0,6)
                + " y=" + std::to_string(ts.pose.y).substr(0,6)
                + " yaw=" + std::to_string((int)ts.pose.yaw)
                + " conf=" + std::to_string(ts.confidence).substr(0,4)
                + vis;
            cv::putText(frame, txt, {40, y},
                        cv::FONT_HERSHEY_SIMPLEX, 1.1, colour, 2);
        };

        drawTag(s0, "Tag0", 50,  {0, 255,   0});
        drawTag(s1, "Tag1", 100, {0, 100, 255});

        auto drawTagBorder = [&](const TagState& ts,
                                 const std::string& label,
                                 const cv::Scalar& colour)
        {
            if (!ts.visible || ts.corners.size() != 4 || srcW <= 0 || srcH <= 0) return;

            const float sx = static_cast<float>(DISPLAY_W) / static_cast<float>(srcW);
            const float sy = static_cast<float>(DISPLAY_H) / static_cast<float>(srcH);

            std::vector<cv::Point> scaled;
            scaled.reserve(4);
            for (const auto& p : ts.corners)
            {
                scaled.emplace_back(
                    static_cast<int>(std::lround(p.x * sx)),
                    static_cast<int>(std::lround(p.y * sy)));
            }

            cv::polylines(frame, scaled, true, colour, 4, cv::LINE_AA);
            cv::putText(frame, label, scaled[0] + cv::Point(0, -10),
                        cv::FONT_HERSHEY_SIMPLEX, 0.9, colour, 2, cv::LINE_AA);
        };

        drawTagBorder(s0, "tag0", {0, 255, 0});
        drawTagBorder(s1, "tag1", {0, 100, 255});

        // Calibration status indicator
        cv::putText(frame,
            calibrated ? "CAL OK" : "CALIBRATING...",
            {40, 150},
            cv::FONT_HERSHEY_SIMPLEX, 1.0,
            calibrated ? cv::Scalar(0,255,100) : cv::Scalar(0,100,255), 2);

        cv::imshow("Tracking", frame);
        if (cv::waitKey(1) == 'q') running = false;
    }
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);

    if (!initUdpSender())
    {
        std::cerr << "[WARN] Failed to initialize UDP sender (127.0.0.1:9001)\n";
    }

    // NOTE: resolution_divider is a C++ constexpr, not a GStreamer variable.
    // The pipeline string must use the computed literal values.
    const int cam_w = 4056 / resolution_divider;
    const int cam_h = 3040 / resolution_divider;

    std::string pipeline =
        "libcamerasrc ! "
        "video/x-raw,width=" + std::to_string(cam_w) +
        ",height="           + std::to_string(cam_h) +
        ",format=BGRx ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink name=sink max-buffers=1 drop=true";

    GError* err = nullptr;
    auto pipe = gst_parse_launch(pipeline.c_str(), &err);
    if (!pipe || err)
    {
        std::cerr << "[ERROR] GStreamer pipeline: "
                  << (err ? err->message : "unknown") << "\n";
        return 1;
    }

    auto sink = gst_bin_get_by_name(GST_BIN(pipe), "sink");
    gst_element_set_state(pipe, GST_STATE_PLAYING);

    std::thread cap  (captureThread, sink);
    std::thread track(trackingThread);
    std::thread vis  (visThread);

    cap.join();
    track.join();
    vis.join();

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    if (udpSock >= 0) close(udpSock);
    return 0;
}
