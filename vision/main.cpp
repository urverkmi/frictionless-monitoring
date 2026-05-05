// ============================================================
//  Dual-camera AprilTag tracker
//
//  Layout:
//    - Two cameras, each independently calibrated against the
//      same rectangular ground-plane marker frame.
//    - Calibration rectangle corners (IDs 2-5) are placed so
//      the short axis fits within each camera's FOV; both cameras
//      see the two central (overlap) markers.
//    - Tracking tags (IDs 0, 1) are fused: if both cameras see
//      a tag the higher-confidence estimate wins; otherwise the
//      single visible estimate is used.
//    - Each JSON line on stdout contains the fused pose + per-
//      camera visibility & confidence for diagnostics.
//    - When both tracking tags are visible, one Detector Contract
//      JSON datagram is sent to UDP 127.0.0.1:9001 for the Python
//      bridge (docs/api.md).
//
//  Calibration rectangle corner IDs and world positions
//  (origin = centre, X = long axis, Y = short axis, Z up):
//
//         ID 2 -------- ID 3
//          |              |
//    CAM_A |   OVERLAP    | CAM_B
//          |              |
//         ID 5 -------- ID 4
//
//  CALIB_W  = full width  (long axis, X)
//  CALIB_H  = full height (short axis, Y)
//  Place CALIB_W so cam-A sees IDs 2,5 fully and
//  the overlap zone shows IDs 2,3 / 5,4 partially.
//  Typical: CALIB_W=1.60 m, CALIB_H=0.60 m.
// ============================================================

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <vector>
#include <array>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

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

constexpr int    RESOLUTION_DIVIDER = 2;

// Physical display size per camera pane
constexpr int    DISPLAY_W          = 1014;    // half of 2028
constexpr int    DISPLAY_H          =  760;    // half of 1520

// AprilTag physical sizes (metres)
constexpr double TAG_SIZE           = 0.10;    // tracking tags  (IDs 0,1)
constexpr double CALIB_TAG_SIZE     = 0.10;    // calibration tags (IDs 2-5)

// Calibration rectangle (metres)
// CALIB_W  = long axis  (camera separation direction, X)
// CALIB_H  = short axis (Y)
// Make CALIB_H short enough that all 4 markers fit in each camera's FOV
// when shooting from the side.  CALIB_W can be as long as needed for
// the desired tracking area width.
constexpr double CALIB_W            = 1.60;
constexpr double CALIB_H            = 0.60;

// Tracking tag IDs
constexpr int    TRACK_TAG0         = 0;
constexpr int    TRACK_TAG1         = 1;

// Confidence weights & saturation values
constexpr double W_MARGIN           = 0.50;
constexpr double W_HAMMING          = 0.25;
constexpr double W_REPROJ           = 0.15;
constexpr double W_SIZE             = 0.10;

constexpr double MARGIN_SAT         = 80.0;    // maps to conf=1
constexpr double SIZE_SAT           = 200.0;   // px diagonal -> conf=1
constexpr double REPROJ_MAX         = 5.0;     // px error   -> conf=0

const std::string CAM_DEVICE_A = "/base/axi/pcie@1000120000/rp1/i2c@88000/imx477@1a";
const std::string CAM_DEVICE_B = "/base/axi/pcie@1000120000/rp1/i2c@80000/imx477@1a";

// UDP sink for Python cpp_stream_bridge (must match docs/api.md)
constexpr const char* DETECTOR_UDP_HOST = "127.0.0.1";
constexpr uint16_t  DETECTOR_UDP_PORT  = 9001;

// ============================================================
// CAMERA INTRINSICS  (same sensor assumed; scale by divider)
// ============================================================

static cv::Mat makeK()
{
    return (cv::Mat1d(3, 3) <<
        4009.22661 / RESOLUTION_DIVIDER, 0.0, 2113.49677 / RESOLUTION_DIVIDER,
        0.0, 4020.48344 / RESOLUTION_DIVIDER, 1469.08894 / RESOLUTION_DIVIDER,
        0.0, 0.0, 1.0);
}

static cv::Mat makeD()
{
    return (cv::Mat1d(1, 5) << -0.49, 0.28, 0.0, 0.0, -0.09);
}

// ============================================================
// CALIBRATION RECTANGLE  -  world corners for each tag
//
// Tags sit AT the corners of the rectangle.
// Corner ordering matches AprilTag CCW p[0..3]:
//   p[0]=TL, p[1]=TR, p[2]=BR, p[3]=BL
//
//  World frame: X right (long axis), Y up (short axis), Z out.
//  Rectangle centre = origin.
//
//   ID 2: top-left   (-W/2,  H/2)   tag grows inward (+X, -Y)
//   ID 3: top-right  ( W/2,  H/2)   tag grows inward (-X, -Y)
//   ID 4: bot-right  ( W/2, -H/2)   tag grows inward (-X, +Y)
//   ID 5: bot-left   (-W/2, -H/2)   tag grows inward (+X, +Y)
// ============================================================

static std::map<int, std::vector<cv::Point3f>> buildWorldTagCorners()
{
    const double W = CALIB_W;
    const double H = CALIB_H;
    const double S = CALIB_TAG_SIZE;

    return {
        // ID 2 - top-left
        {2, { {(float)(-W/2),   (float)( H/2),   0},
              {(float)(-W/2+S), (float)( H/2),   0},
              {(float)(-W/2+S), (float)( H/2-S), 0},
              {(float)(-W/2),   (float)( H/2-S), 0} }},

        // ID 3 - top-right
        {3, { {(float)( W/2-S), (float)( H/2),   0},
              {(float)( W/2),   (float)( H/2),   0},
              {(float)( W/2),   (float)( H/2-S), 0},
              {(float)( W/2-S), (float)( H/2-S), 0} }},

        // ID 4 - bot-right
        {4, { {(float)( W/2-S), (float)(-H/2+S), 0},
              {(float)( W/2),   (float)(-H/2+S), 0},
              {(float)( W/2),   (float)(-H/2),   0},
              {(float)( W/2-S), (float)(-H/2),   0} }},

        // ID 5 - bot-left
        {5, { {(float)(-W/2),   (float)(-H/2+S), 0},
              {(float)(-W/2+S), (float)(-H/2+S), 0},
              {(float)(-W/2+S), (float)(-H/2),   0},
              {(float)(-W/2),   (float)(-H/2),   0} }}
    };
}

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
    double confidence = 0.0;
    bool   visible    = false;
};

// ============================================================
// CAMERA CONTEXT  -  everything belonging to one camera
// ============================================================

struct CameraContext
{
    int         id;
    std::string device;

    // GStreamer
    GstElement* pipeline = nullptr;
    GstElement* sink     = nullptr;

    // Frame buffer
    std::mutex  frameMutex;
    cv::Mat     latestFrame;
    std::atomic<uint64_t> lastFrameNs{0};

    // Intrinsics
    cv::Mat     K;
    cv::Mat     D;

    // Extrinsics (camera -> world)
    cv::Mat     R_wc;
    cv::Mat     t_wc;
    std::atomic<bool> calibrated{false};

    // Latest per-frame observations
    std::mutex  obsMutex;
    TagState    tag0obs;
    TagState    tag1obs;
};

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool>     running(true);
std::atomic<uint64_t> frameCounter(0);

std::array<CameraContext, 2> cameras;

std::mutex   fusedMutex;
TagState     fusedTag0;
TagState     fusedTag1;

static const auto worldTagCorners = buildWorldTagCorners();

// ============================================================
// HELPERS
// ============================================================

static inline double clamp01(double x)
{
    return std::max(0.0, std::min(1.0, x));
}

static double tagPixelDiagonal(apriltag_detection_t* det)
{
    double dx = det->p[2][0] - det->p[0][0];
    double dy = det->p[2][1] - det->p[0][1];
    return std::sqrt(dx*dx + dy*dy);
}

static double reprojectionError(
    const std::vector<cv::Point3f>& obj,
    const std::vector<cv::Point2f>& img,
    const cv::Mat& rvec,
    const cv::Mat& tvec,
    const cv::Mat& K,
    const cv::Mat& D)
{
    std::vector<cv::Point2f> proj;
    cv::projectPoints(obj, rvec, tvec, K, D, proj);
    double err = 0.0;
    for (size_t i = 0; i < img.size(); ++i)
    {
        double dx = img[i].x - proj[i].x;
        double dy = img[i].y - proj[i].y;
        err += std::sqrt(dx*dx + dy*dy);
    }
    return err / static_cast<double>(img.size());
}

static double computeConfidence(
    apriltag_detection_t* det,
    const cv::Mat& rvec,
    const cv::Mat& tvec,
    const std::vector<cv::Point3f>& obj,
    const std::vector<cv::Point2f>& img,
    const cv::Mat& K,
    const cv::Mat& D)
{
    double s_margin = clamp01(det->decision_margin / MARGIN_SAT);

    double s_hamming;
    switch (det->hamming)
    {
        case 0:  s_hamming = 1.00; break;
        case 1:  s_hamming = 0.50; break;
        default: s_hamming = 0.00; break;
    }

    double reproj   = reprojectionError(obj, img, rvec, tvec, K, D);
    double s_reproj = clamp01(1.0 - reproj / REPROJ_MAX);

    double diag   = tagPixelDiagonal(det);
    double s_size = clamp01(diag / SIZE_SAT);

    return clamp01(
        W_MARGIN  * s_margin  +
        W_HAMMING * s_hamming +
        W_REPROJ  * s_reproj  +
        W_SIZE    * s_size);
}

static cv::Point3f camToWorld(
    const cv::Mat& tvec,
    const cv::Mat& R_wc,
    const cv::Mat& t_wc)
{
    if (R_wc.empty() || t_wc.empty()) return {0, 0, 0};
    cv::Mat p = R_wc * tvec + t_wc;
    return { (float)p.at<double>(0),
             (float)p.at<double>(1),
             (float)p.at<double>(2) };
}

// ============================================================
// APRILTAG DETECTOR FACTORY
// ============================================================

static apriltag_detector_t* createDetector()
{
    auto tf = tag36h11_create();
    auto td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = 2.0;
    td->nthreads      = 4;
    td->refine_edges  = 1;
    return td;
}

// ============================================================
// GSTREAMER PIPELINE BUILDER
// ============================================================

static std::string buildPipeline(const std::string& device)
{
    return
        "libcamerasrc camera-name=" + device + " ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        // Keep only freshest frame and do not sync to clock.
        "appsink name=sink max-buffers=1 drop=true sync=false";
}

// ============================================================
// PER-CAMERA CAPTURE THREAD
// ============================================================

void captureThread(CameraContext& cam)
{
    while (running)
    {
        auto sample = gst_app_sink_try_pull_sample(
            GST_APP_SINK(cam.sink), 200 * GST_MSECOND);
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
            std::lock_guard<std::mutex> lock(cam.frameMutex);
            cam.latestFrame = frame.clone();
            const auto nowNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            cam.lastFrameNs = static_cast<uint64_t>(nowNs);
        }

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
    }
}

// ============================================================
// CALIBRATION  (per camera, called from tracking thread)
// ============================================================

static bool calibrateCamera(
    CameraContext& cam,
    std::vector<cv::Point2f>& imgPts,
    std::vector<cv::Point3f>& objPts)
{
    if (imgPts.size() < 8) return false;

    cv::Mat rvec;
    cv::solvePnP(objPts, imgPts, cam.K, cam.D, rvec, cam.t_wc);
    cv::Rodrigues(rvec, cam.R_wc);
    cam.calibrated = true;

    std::cerr << "[INFO] Camera " << cam.id
              << " calibrated (" << imgPts.size() << " pts)\n";
    return true;
}

// ============================================================
// PER-CAMERA TRACKING THREAD
// ============================================================

void trackingThread(CameraContext& cam)
{
    auto detector = createDetector();

    const std::vector<cv::Point3f> trackObj =
    {
        {-(float)TAG_SIZE/2, -(float)TAG_SIZE/2, 0},
        { (float)TAG_SIZE/2, -(float)TAG_SIZE/2, 0},
        { (float)TAG_SIZE/2,  (float)TAG_SIZE/2, 0},
        {-(float)TAG_SIZE/2,  (float)TAG_SIZE/2, 0}
    };

    while (running)
    {
        cv::Mat frame;
        uint64_t frameTsNs = 0;
        {
            std::lock_guard<std::mutex> lock(cam.frameMutex);
            if (cam.latestFrame.empty()) continue;
            frame = cam.latestFrame.clone();
            frameTsNs = cam.lastFrameNs.load();
        }

        // If camera feed goes stale, invalidate observations so UI/bridge
        // do not keep publishing frozen values.
        const auto nowNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const uint64_t staleThresholdNs = 500ULL * 1000ULL * 1000ULL; // 500 ms
        if (frameTsNs == 0 ||
            static_cast<uint64_t>(nowNs) - frameTsNs > staleThresholdNs)
        {
            TagState lost0, lost1;
            lost0.visible = false;
            lost1.visible = false;
            {
                std::lock_guard<std::mutex> lock(cam.obsMutex);
                cam.tag0obs = lost0;
                cam.tag1obs = lost1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        ++frameCounter;

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        image_u8_t img =
        {
            gray.cols, gray.rows, gray.cols, gray.data
        };

        auto detections = apriltag_detector_detect(detector, &img);

        std::vector<cv::Point2f> calibImg;
        std::vector<cv::Point3f> calibObj;

        TagState new0, new1;
        new0.visible = false;
        new1.visible = false;

        for (int i = 0; i < zarray_size(detections); ++i)
        {
            apriltag_detection_t* det;
            zarray_get(detections, i, &det);

            if (det->hamming > 1) continue;

            // --- Calibration corners ---
            if (worldTagCorners.count(det->id))
            {
                for (int k = 0; k < 4; ++k)
                {
                    calibImg.emplace_back(det->p[k][0], det->p[k][1]);
                    calibObj.push_back(worldTagCorners.at(det->id)[k]);
                }
            }

            // --- Tracking tags ---
            if (cam.calibrated &&
                (det->id == TRACK_TAG0 || det->id == TRACK_TAG1))
            {
                std::vector<cv::Point2f> imgPts;
                for (int k = 0; k < 4; ++k)
                    imgPts.emplace_back(det->p[k][0], det->p[k][1]);

                cv::Mat rvec, tvec;
                cv::solvePnP(trackObj, imgPts, cam.K, cam.D, rvec, tvec);

                double conf = computeConfidence(
                    det, rvec, tvec, trackObj, imgPts, cam.K, cam.D);

                auto world = camToWorld(tvec, cam.R_wc, cam.t_wc);

                cv::Mat R;
                cv::Rodrigues(rvec, R);
                double yaw = std::atan2(R.at<double>(1, 0),
                                        R.at<double>(0, 0)) * 180.0 / CV_PI;

                TagState ts;
                ts.pose       = { world.x, world.y, yaw };
                ts.confidence = conf;
                ts.visible    = true;

                if (det->id == TRACK_TAG0) new0 = ts;
                else                        new1 = ts;
            }
        }

        if (!cam.calibrated)
            calibrateCamera(cam, calibImg, calibObj);

        {
            std::lock_guard<std::mutex> lock(cam.obsMutex);
            cam.tag0obs = new0;
            cam.tag1obs = new1;
        }

        apriltag_detections_destroy(detections);
    }
}

// ============================================================
// FUSION + JSON OUTPUT THREAD
//
// Reads latest observations from both cameras, picks the
// higher-confidence estimate for each tag, writes fused state
// and emits one JSON line per iteration (~100 Hz).
// When both tags are visible, also sends one Detector Contract
// JSON datagram per iteration to UDP localhost:9001.
// ============================================================

void fusionThread()
{
    int udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in udpAddr{};
    if (udpFd >= 0)
    {
        udpAddr.sin_family = AF_INET;
        udpAddr.sin_port   = htons(DETECTOR_UDP_PORT);
        if (inet_pton(AF_INET, DETECTOR_UDP_HOST, &udpAddr.sin_addr) != 1)
        {
            close(udpFd);
            udpFd = -1;
        }
    }
    if (udpFd < 0)
        std::cerr << "[WARN] detector UDP socket unavailable; start Python bridge before vision\n";

    while (running)
    {
        // Snapshot observations from both cameras
        TagState obs[2][2];   // obs[camIdx][tagIdx 0/1]
        for (int c = 0; c < 2; ++c)
        {
            std::lock_guard<std::mutex> lock(cameras[c].obsMutex);
            obs[c][0] = cameras[c].tag0obs;
            obs[c][1] = cameras[c].tag1obs;
        }

        // Fuse each tracking tag: prefer higher confidence
        auto fuse = [&](int tagIdx) -> TagState
        {
            const TagState& a = obs[0][tagIdx];
            const TagState& b = obs[1][tagIdx];

            if ( a.visible && !b.visible) return a;
            if (!a.visible &&  b.visible) return b;
            if (!a.visible && !b.visible) return {};

            // Both visible: pick higher confidence winner
            return (a.confidence >= b.confidence) ? a : b;
        };

        TagState f0 = fuse(0);
        TagState f1 = fuse(1);

        {
            std::lock_guard<std::mutex> lock(fusedMutex);
            fusedTag0 = f0;
            fusedTag1 = f1;
        }

        // --- Detector contract -> Python (UDP), docs/api.md ---
        if (udpFd >= 0 && f0.visible && f1.visible)
        {
            const auto wallNow = std::chrono::system_clock::now();
            const double tsSec = std::chrono::duration<double>(
                                     wallNow.time_since_epoch())
                                     .count();

            const double dx = f1.pose.x - f0.pose.x;
            const double dy = f1.pose.y - f0.pose.y;
            double orbit = std::atan2(dy, dx);
            if (orbit < 0.0)
                orbit += 2.0 * std::acos(-1.0);

            const double trackConf =
                std::min(f0.confidence, f1.confidence);

            std::ostringstream det;
            det.setf(std::ios::fixed);
            det << std::setprecision(6)
                << "{\"timestamp\":" << tsSec
                << ",\"frame_id\":" << frameCounter.load()
                << ",\"camera_id\":\"fused\""
                << ",\"satellite_position\":{\"x\":" << f0.pose.x
                << ",\"y\":" << f0.pose.y << '}'
                << ",\"end_mass_position\":{\"x\":" << f1.pose.x
                << ",\"y\":" << f1.pose.y << '}'
                << ",\"orbital_angular_position\":" << orbit
                << ",\"tracking_confidence\":";
            det << std::setprecision(4) << trackConf << '}';

            const std::string detStr = det.str();
            (void)sendto(
                udpFd,
                detStr.data(),
                detStr.size(),
                0,
                reinterpret_cast<const sockaddr*>(&udpAddr),
                static_cast<socklen_t>(sizeof(udpAddr)));
        }

        // --- JSON helpers ---
        auto fp = [](double v, int p = 4) -> std::string
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(p) << v;
            return oss.str();
        };

        auto camDiag = [&](const TagState& ts) -> std::string
        {
            return "{\"conf\":"    + fp(ts.confidence, 3) +
                   ",\"visible\":" + (ts.visible ? "true" : "false") +
                   "}";
        };

        auto tagJson = [&](const std::string& key,
                           const TagState& fused,
                           int tagIdx) -> std::string
        {
            return "\"" + key + "\":{"
                "\"x\":"        + fp(fused.pose.x)       +
                ",\"y\":"       + fp(fused.pose.y)        +
                ",\"yaw\":"     + fp(fused.pose.yaw)      +
                ",\"conf\":"    + fp(fused.confidence, 3) +
                ",\"visible\":" + (fused.visible ? "true" : "false") +
                ",\"camA\":"    + camDiag(obs[0][tagIdx]) +
                ",\"camB\":"    + camDiag(obs[1][tagIdx]) +
                "}";
        };

        auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        std::cout
            << "{"
            << "\"ts\":"     << ts_ns
            << ",\"frame\":" << frameCounter.load()
            << ",\"calA\":"  << (cameras[0].calibrated ? "true" : "false")
            << ",\"calB\":"  << (cameras[1].calibrated ? "true" : "false")
            << ","           << tagJson("tag0", f0, 0)
            << ","           << tagJson("tag1", f1, 1)
            << "}\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (udpFd >= 0)
        close(udpFd);
}

// ============================================================
// VISUALIZATION THREAD
// Both camera feeds side-by-side with per-camera and fused info
// ============================================================

void visThread()
{
    cv::namedWindow("Tracking", cv::WINDOW_NORMAL);
    cv::resizeWindow("Tracking", DISPLAY_W * 2, DISPLAY_H);

    while (running)
    {
        std::array<cv::Mat, 2> frames;
        for (int c = 0; c < 2; ++c)
        {
            std::lock_guard<std::mutex> lock(cameras[c].frameMutex);
            if (cameras[c].latestFrame.empty())
                frames[c] = cv::Mat::zeros(DISPLAY_H, DISPLAY_W, CV_8UC3);
            else
                cv::resize(cameras[c].latestFrame, frames[c],
                           cv::Size(DISPLAY_W, DISPLAY_H));
        }

        TagState s0, s1;
        {
            std::lock_guard<std::mutex> lock(fusedMutex);
            s0 = fusedTag0;
            s1 = fusedTag1;
        }

        // --- Per-camera overlay ---
        for (int c = 0; c < 2; ++c)
        {
            cv::Mat& f = frames[c];

            cv::putText(f, "CAM " + std::string(c == 0 ? "A" : "B"),
                        {20, 40}, cv::FONT_HERSHEY_SIMPLEX, 1.3,
                        {200, 200, 200}, 2);

            bool cal = cameras[c].calibrated.load();
            cv::putText(f, cal ? "CAL OK" : "CALIBRATING",
                        {20, 80}, cv::FONT_HERSHEY_SIMPLEX, 0.9,
                        cal ? cv::Scalar(0, 220, 80) : cv::Scalar(0, 80, 220), 2);

            TagState camObs[2];
            {
                std::lock_guard<std::mutex> lock(cameras[c].obsMutex);
                camObs[0] = cameras[c].tag0obs;
                camObs[1] = cameras[c].tag1obs;
            }

            auto obsLine = [&](const TagState& ts,
                               const std::string& lbl, int y, cv::Scalar col)
            {
                std::ostringstream oss;
                oss << lbl << (ts.visible ? "" : " [--]")
                    << " cf=" << std::fixed << std::setprecision(2)
                    << ts.confidence;
                cv::putText(f, oss.str(), {20, y},
                            cv::FONT_HERSHEY_SIMPLEX, 0.85, col, 2);
            };

            obsLine(camObs[0], "T0", 120, {80,  255,  80});
            obsLine(camObs[1], "T1", 155, {80,  140, 255});
        }

        // --- Fused overlay on left pane ---
        auto fusedLine = [&](const TagState& ts,
                              const std::string& lbl, int y, cv::Scalar col)
        {
            std::ostringstream oss;
            oss << "[FUSED] " << lbl;
            if (ts.visible)
                oss << " x=" << std::fixed << std::setprecision(3) << ts.pose.x
                    << " y=" << ts.pose.y
                    << " yaw=" << std::setprecision(1) << ts.pose.yaw
                    << " cf=" << std::setprecision(2) << ts.confidence;
            else
                oss << " [lost]";
            cv::putText(frames[0], oss.str(), {20, y},
                        cv::FONT_HERSHEY_SIMPLEX, 0.85, col, 2);
        };

        fusedLine(s0, "T0", 210, {0, 255, 130});
        fusedLine(s1, "T1", 245, {0, 130, 255});

        // Side-by-side
        cv::Mat composite;
        cv::hconcat(frames[0], frames[1], composite);
        cv::imshow("Tracking", composite);

        if (cv::waitKey(1) == 'q') running = false;
    }
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    bool enableVis = true;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--no-vis") == 0)
            enableVis = false;
    }

    // Initialise camera contexts
    cameras[0].id     = 0;
    cameras[0].device = CAM_DEVICE_A;
    cameras[0].K      = makeK();
    cameras[0].D      = makeD();

    cameras[1].id     = 1;
    cameras[1].device = CAM_DEVICE_B;
    cameras[1].K      = makeK();
    cameras[1].D      = makeD();

    // Build and start GStreamer pipelines
    for (int c = 0; c < 2; ++c)
    {
        GError* gerr = nullptr;
        std::string pipeStr = buildPipeline(cameras[c].device);

        cameras[c].pipeline = gst_parse_launch(pipeStr.c_str(), &gerr);
        if (!cameras[c].pipeline || gerr)
        {
            std::cerr << "[ERROR] Camera " << c << " pipeline: "
                      << (gerr ? gerr->message : "unknown") << "\n";
            return 1;
        }

        cameras[c].sink =
            gst_bin_get_by_name(GST_BIN(cameras[c].pipeline), "sink");

        gst_element_set_state(cameras[c].pipeline, GST_STATE_PLAYING);
        std::cerr << "[INFO] Camera " << c
                  << " pipeline started (" << cameras[c].device << ")\n";
    }

    // Launch threads:  2x capture  +  2x tracking  +  fusion  +  optional vis
    std::thread capA   (captureThread,  std::ref(cameras[0]));
    std::thread capB   (captureThread,  std::ref(cameras[1]));
    std::thread trackA (trackingThread, std::ref(cameras[0]));
    std::thread trackB (trackingThread, std::ref(cameras[1]));
    std::thread fusion (fusionThread);
    std::thread vis;
    if (enableVis)
        vis = std::thread(visThread);
    else
        std::cerr << "[INFO] Visualization disabled (--no-vis)\n";

    capA.join();
    capB.join();
    trackA.join();
    trackB.join();
    fusion.join();
    if (vis.joinable())
        vis.join();

    for (int c = 0; c < 2; ++c)
    {
        gst_element_set_state(cameras[c].pipeline, GST_STATE_NULL);
        gst_object_unref(cameras[c].pipeline);
    }

    return 0;
}
