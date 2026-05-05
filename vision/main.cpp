// ============================================================
//  Dual-camera AprilTag tracker
//
//  Camera geometry
//  ---------------
//  Both cameras are mounted side-by-side in landscape orientation.
//  The physical overlap zone appears at the BOTTOM edge of each
//  camera's image.
//
//  To align the composite so calibration markers line up:
//    • Camera A is displayed normally (overlap at bottom = center of stitch).
//    • Camera B is flipped vertically so its overlap (originally at bottom)
//      appears at the TOP of its pane, which is placed to the right of A.
//
//  Composite layout (top view of physical scene):
//
//    ┌─────────────┬──────────┬─────────────┐
//    │  Cam-A only │ OVERLAP  │ Cam-B only  │
//    │             │ (blended)│             │
//    └─────────────┴──────────┴─────────────┘
//                  ↑                    ↑
//             bottom of A          top of B (after vflip)
//
//  Calibration rectangle (IDs 2-5) world coordinates
//  --------------------------------------------------
//  Origin = centre, X = long axis, Y = short axis, Z up.
//
//         ID 2 ──────────────── ID 3
//          │                      │
//    CAM A │       OVERLAP        │ CAM B
//          │                      │
//         ID 5 ──────────────── ID 4
//
//  CALIB_W (long, X) = full width of tracking area.
//  CALIB_H (short, Y) = narrow enough that ALL 4 markers are
//                        visible by each camera.
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

// IMX477 native resolution
constexpr int    CAM_NATIVE_W       = 4056;
constexpr int    CAM_NATIVE_H       = 3040;
constexpr int    RESOLUTION_DIVIDER = 2;
constexpr int    CAM_W              = CAM_NATIVE_W / RESOLUTION_DIVIDER;  // 2028
constexpr int    CAM_H              = CAM_NATIVE_H / RESOLUTION_DIVIDER;  // 1520

// Display: each camera pane is scaled down for the composite window
constexpr int    DISPLAY_SCALE      = 2;   // divide CAM size for display
constexpr int    PANE_W             = CAM_W / DISPLAY_SCALE;              // 1014
constexpr int    PANE_H             = CAM_H / DISPLAY_SCALE;              // 760

// Overlap zone height in DISPLAY pixels (rows at bottom of A / top of B-flipped)
// Tune this to match the physical overlap fraction of your camera placement.
constexpr int    OVERLAP_PX         = 150;   // ~20 % of pane height

// AprilTag physical sizes (metres)
constexpr double TAG_SIZE           = 0.10;  // tracking tags  IDs 0, 1
constexpr double CALIB_TAG_SIZE     = 0.10;  // calibration tags IDs 2-5

// Calibration rectangle (metres)
constexpr double CALIB_W            = 1.60;  // long axis  (X)
constexpr double CALIB_H            = 0.60;  // short axis (Y)

// Tracking tag IDs
constexpr int    TRACK_TAG0         = 0;
constexpr int    TRACK_TAG1         = 1;

// Confidence weights & saturation values
constexpr double W_MARGIN           = 0.50;
constexpr double W_HAMMING          = 0.25;
constexpr double W_REPROJ           = 0.15;
constexpr double W_SIZE             = 0.10;
constexpr double MARGIN_SAT         = 80.0;
constexpr double SIZE_SAT           = 200.0;
constexpr double REPROJ_MAX         = 5.0;

// libcamera device identifiers for the two IMX477 sensors
const std::string CAM_DEVICE_A =
    "/base/axi/pcie@1000120000/rp1/i2c@88000/imx477@1a";
const std::string CAM_DEVICE_B =
    "/base/axi/pcie@1000120000/rp1/i2c@80000/imx477@1a";

// UDP output (Detector Contract → Python bridge)
constexpr const char* DETECTOR_UDP_HOST = "127.0.0.1";
constexpr uint16_t    DETECTOR_UDP_PORT = 9001;

// ============================================================
// CAMERA INTRINSICS
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
// CALIBRATION RECTANGLE WORLD CORNERS
// ============================================================

static std::map<int, std::vector<cv::Point3f>> buildWorldTagCorners()
{
    const double W = CALIB_W, H = CALIB_H, S = CALIB_TAG_SIZE;
    return {
        // ID 2 – top-left, grows inward (+X, -Y)
        {2, { {(float)(-W/2),   (float)( H/2),   0},
              {(float)(-W/2+S), (float)( H/2),   0},
              {(float)(-W/2+S), (float)( H/2-S), 0},
              {(float)(-W/2),   (float)( H/2-S), 0} }},
        // ID 3 – top-right, grows inward (-X, -Y)
        {3, { {(float)( W/2-S), (float)( H/2),   0},
              {(float)( W/2),   (float)( H/2),   0},
              {(float)( W/2),   (float)( H/2-S), 0},
              {(float)( W/2-S), (float)( H/2-S), 0} }},
        // ID 4 – bot-right, grows inward (-X, +Y)
        {4, { {(float)( W/2-S), (float)(-H/2+S), 0},
              {(float)( W/2),   (float)(-H/2+S), 0},
              {(float)( W/2),   (float)(-H/2),   0},
              {(float)( W/2-S), (float)(-H/2),   0} }},
        // ID 5 – bot-left, grows inward (+X, +Y)
        {5, { {(float)(-W/2),   (float)(-H/2+S), 0},
              {(float)(-W/2+S), (float)(-H/2+S), 0},
              {(float)(-W/2+S), (float)(-H/2),   0},
              {(float)(-W/2),   (float)(-H/2),   0} }}
    };
}

// ============================================================
// DATA STRUCTS
// ============================================================

struct Pose   { double x=0, y=0, yaw=0; };

struct TagState
{
    Pose   pose;
    double confidence = 0.0;
    bool   visible    = false;
};

// Detected quad corners in pixel space (for visualisation)
struct DetectionBox
{
    std::array<cv::Point2f, 4> corners;
    bool valid = false;
};

// ============================================================
// CAMERA CONTEXT
// ============================================================

struct CameraContext
{
    int         id;
    std::string device;

    GstElement* pipeline = nullptr;
    GstElement* sink     = nullptr;

    std::mutex frameMutex;
    cv::Mat    latestFrame;          // full-resolution, BGR

    cv::Mat K, D;                    // intrinsics
    cv::Mat R_wc, t_wc;             // extrinsics (cam → world)
    std::atomic<bool> calibrated{false};

    std::mutex  obsMutex;
    TagState    tag0obs, tag1obs;
    DetectionBox box0obs, box1obs;   // raw pixel quads for vis overlay
};

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool>     running(true);
std::atomic<uint64_t> frameCounter(0);

std::array<CameraContext, 2> cameras;

std::mutex fusedMutex;
TagState   fusedTag0, fusedTag1;

static const auto worldTagCorners = buildWorldTagCorners();

// ============================================================
// HELPERS
// ============================================================

static inline double clamp01(double x)
{ return std::max(0.0, std::min(1.0, x)); }

static double tagPixelDiagonal(apriltag_detection_t* d)
{
    double dx = d->p[2][0]-d->p[0][0], dy = d->p[2][1]-d->p[0][1];
    return std::sqrt(dx*dx+dy*dy);
}

static double reprojectionError(
    const std::vector<cv::Point3f>& obj,
    const std::vector<cv::Point2f>& img,
    const cv::Mat& rvec, const cv::Mat& tvec,
    const cv::Mat& K,   const cv::Mat& D)
{
    std::vector<cv::Point2f> proj;
    cv::projectPoints(obj, rvec, tvec, K, D, proj);
    double err = 0;
    for (size_t i = 0; i < img.size(); ++i)
    {
        double dx = img[i].x-proj[i].x, dy = img[i].y-proj[i].y;
        err += std::sqrt(dx*dx+dy*dy);
    }
    return err / img.size();
}

static double computeConfidence(
    apriltag_detection_t* det,
    const cv::Mat& rvec, const cv::Mat& tvec,
    const std::vector<cv::Point3f>& obj,
    const std::vector<cv::Point2f>& img,
    const cv::Mat& K, const cv::Mat& D)
{
    double s_margin  = clamp01(det->decision_margin / MARGIN_SAT);
    double s_hamming = (det->hamming==0) ? 1.0 : (det->hamming==1) ? 0.5 : 0.0;
    double s_reproj  = clamp01(1.0 - reprojectionError(obj,img,rvec,tvec,K,D) / REPROJ_MAX);
    double s_size    = clamp01(tagPixelDiagonal(det) / SIZE_SAT);
    return clamp01(W_MARGIN*s_margin + W_HAMMING*s_hamming +
                   W_REPROJ*s_reproj + W_SIZE*s_size);
}

static cv::Point3f camToWorld(const cv::Mat& tvec,
                               const cv::Mat& R_wc, const cv::Mat& t_wc)
{
    if (R_wc.empty()||t_wc.empty()) return {0,0,0};
    cv::Mat p = R_wc*tvec + t_wc;
    return {(float)p.at<double>(0),(float)p.at<double>(1),(float)p.at<double>(2)};
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
// GSTREAMER PIPELINE
//
// Explicitly negotiate the full (divided) resolution so libcamera
// does not fall back to a smaller mode and crop the sensor.
// ============================================================

static std::string buildPipeline(const std::string& device)
{
    return
        "libcamerasrc camera-name=" + device + " ! "
        "video/x-raw,"
        "width="  + std::to_string(CAM_W) + ","
        "height=" + std::to_string(CAM_H) + ","
        "format=BGRx ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink name=sink max-buffers=1 drop=true sync=false";
}

// ============================================================
// CAPTURE THREAD
// ============================================================

void captureThread(CameraContext& cam)
{
    while (running)
    {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(cam.sink));
        if (!sample) continue;

        GstBuffer*  buffer = gst_sample_get_buffer(sample);
        GstMapInfo  map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        GstCaps*      caps = gst_sample_get_caps(sample);
        GstStructure* s    = gst_caps_get_structure(caps, 0);
        int w, h;
        gst_structure_get_int(s, "width",  &w);
        gst_structure_get_int(s, "height", &h);

        cv::Mat frame(h, w, CV_8UC3, map.data);
        {
            std::lock_guard<std::mutex> lk(cam.frameMutex);
            cam.latestFrame = frame.clone();
        }

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
    }
}

// ============================================================
// CALIBRATION
// ============================================================

static bool calibrateCamera(CameraContext& cam,
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
// TRACKING THREAD
// ============================================================

void trackingThread(CameraContext& cam)
{
    auto* detector = createDetector();

    const std::vector<cv::Point3f> trackObj = {
        {-(float)TAG_SIZE/2, -(float)TAG_SIZE/2, 0},
        { (float)TAG_SIZE/2, -(float)TAG_SIZE/2, 0},
        { (float)TAG_SIZE/2,  (float)TAG_SIZE/2, 0},
        {-(float)TAG_SIZE/2,  (float)TAG_SIZE/2, 0}
    };

    while (running)
    {
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lk(cam.frameMutex);
            if (cam.latestFrame.empty()) continue;
            frame = cam.latestFrame.clone();
        }
        ++frameCounter;

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        image_u8_t img { gray.cols, gray.rows, gray.cols, gray.data };

        auto* dets = apriltag_detector_detect(detector, &img);

        std::vector<cv::Point2f> calibImg;
        std::vector<cv::Point3f> calibObj;
        TagState    new0, new1;
        DetectionBox nb0, nb1;
        new0.visible = new1.visible = false;
        nb0.valid    = nb1.valid    = false;

        for (int i = 0; i < zarray_size(dets); ++i)
        {
            apriltag_detection_t* det;
            zarray_get(dets, i, &det);

            if (det->hamming > 1) continue;

            // -- calibration corners --
            if (worldTagCorners.count(det->id))
                for (int k = 0; k < 4; ++k)
                {
                    calibImg.emplace_back(det->p[k][0], det->p[k][1]);
                    calibObj.push_back(worldTagCorners.at(det->id)[k]);
                }

            // -- tracking tags --
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
                cv::Mat R; cv::Rodrigues(rvec, R);
                double yaw = std::atan2(R.at<double>(1,0),
                                        R.at<double>(0,0)) * 180.0/CV_PI;

                TagState ts;
                ts.pose       = { world.x, world.y, yaw };
                ts.confidence = conf;
                ts.visible    = true;

                // Store pixel quad for visualisation
                DetectionBox box;
                for (int k = 0; k < 4; ++k)
                    box.corners[k] = { (float)det->p[k][0], (float)det->p[k][1] };
                box.valid = true;

                if (det->id == TRACK_TAG0) { new0=ts; nb0=box; }
                else                        { new1=ts; nb1=box; }
            }
        }

        if (!cam.calibrated)
            calibrateCamera(cam, calibImg, calibObj);

        {
            std::lock_guard<std::mutex> lk(cam.obsMutex);
            cam.tag0obs = new0; cam.box0obs = nb0;
            cam.tag1obs = new1; cam.box1obs = nb1;
        }

        apriltag_detections_destroy(dets);
    }
}

// ============================================================
// FUSION + UDP + JSON OUTPUT THREAD
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
        { close(udpFd); udpFd = -1; }
    }
    if (udpFd < 0)
        std::cerr << "[WARN] UDP socket unavailable\n";

    while (running)
    {
        TagState obs[2][2];
        for (int c = 0; c < 2; ++c)
        {
            std::lock_guard<std::mutex> lk(cameras[c].obsMutex);
            obs[c][0] = cameras[c].tag0obs;
            obs[c][1] = cameras[c].tag1obs;
        }

        auto fuse = [&](int ti) -> TagState
        {
            const TagState& a = obs[0][ti];
            const TagState& b = obs[1][ti];
            if ( a.visible && !b.visible) return a;
            if (!a.visible &&  b.visible) return b;
            if (!a.visible && !b.visible) return {};
            return (a.confidence >= b.confidence) ? a : b;
        };

        TagState f0 = fuse(0), f1 = fuse(1);
        { std::lock_guard<std::mutex> lk(fusedMutex); fusedTag0=f0; fusedTag1=f1; }

        // UDP Detector Contract
        if (udpFd >= 0 && f0.visible && f1.visible)
        {
            double tsSec = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            double dx = f1.pose.x-f0.pose.x, dy = f1.pose.y-f0.pose.y;
            double orbit = std::atan2(dy, dx);
            if (orbit < 0) orbit += 2*std::acos(-1.0);
            double tc = std::min(f0.confidence, f1.confidence);

            std::ostringstream d;
            d << std::fixed << std::setprecision(6)
              << "{\"timestamp\":"  << tsSec
              << ",\"frame_id\":"   << frameCounter.load()
              << ",\"camera_id\":\"fused\""
              << ",\"satellite_position\":{\"x\":" << f0.pose.x << ",\"y\":" << f0.pose.y << "}"
              << ",\"end_mass_position\":{\"x\":"  << f1.pose.x << ",\"y\":" << f1.pose.y << "}"
              << ",\"orbital_angular_position\":"  << orbit
              << ",\"tracking_confidence\":"
              << std::setprecision(4) << tc << "}";
            std::string s = d.str();
            (void)sendto(udpFd, s.data(), s.size(), 0,
                         reinterpret_cast<const sockaddr*>(&udpAddr),
                         sizeof(udpAddr));
        }

        // Stdout JSON
        auto fp = [](double v, int p=4) {
            std::ostringstream o; o<<std::fixed<<std::setprecision(p)<<v; return o.str(); };
        auto camDiag = [&](const TagState& ts) {
            return "{\"conf\":"+fp(ts.confidence,3)+
                   ",\"visible\":"+(ts.visible?"true":"false")+"}"; };
        auto tagJson = [&](const std::string& key,
                           const TagState& fused, int ti) {
            return "\""+key+"\":{"
                "\"x\":"+fp(fused.pose.x)+
                ",\"y\":"+fp(fused.pose.y)+
                ",\"yaw\":"+fp(fused.pose.yaw)+
                ",\"conf\":"+fp(fused.confidence,3)+
                ",\"visible\":"+(fused.visible?"true":"false")+
                ",\"camA\":"+camDiag(obs[0][ti])+
                ",\"camB\":"+camDiag(obs[1][ti])+"}"; };

        auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        std::cout
            << "{\"ts\":"     << ts_ns
            << ",\"frame\":"  << frameCounter.load()
            << ",\"calA\":"   << (cameras[0].calibrated?"true":"false")
            << ",\"calB\":"   << (cameras[1].calibrated?"true":"false")
            << ","            << tagJson("tag0",f0,0)
            << ","            << tagJson("tag1",f1,1)
            << "}\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (udpFd >= 0) close(udpFd);
}

// ============================================================
// DRAW DETECTION BOX  (full-resolution pixel coords)
//
// Draws a rotated quad + label around a detected tracking tag.
// Scales corners from full-res to display-pane coords first.
// ============================================================

static void drawDetectionBox(cv::Mat& pane,
                              const DetectionBox& box,
                              const TagState& ts,
                              const std::string& label,
                              cv::Scalar colour)
{
    if (!box.valid) return;

    // Scale factor from full-res camera to display pane
    const float sx = (float)PANE_W / CAM_W;
    const float sy = (float)PANE_H / CAM_H;

    std::array<cv::Point, 4> pts;
    for (int k = 0; k < 4; ++k)
        pts[k] = { (int)(box.corners[k].x * sx),
                   (int)(box.corners[k].y * sy) };

    // Draw the quad
    for (int k = 0; k < 4; ++k)
        cv::line(pane, pts[k], pts[(k+1)%4], colour, 2, cv::LINE_AA);

    // Corner dots
    for (auto& p : pts)
        cv::circle(pane, p, 4, colour, -1);

    // Label above TL corner
    std::ostringstream oss;
    oss << label;
    if (ts.visible)
        oss << " cf=" << std::fixed << std::setprecision(2) << ts.confidence;
    cv::putText(pane, oss.str(),
                { pts[0].x, std::max(pts[0].y - 8, 14) },
                cv::FONT_HERSHEY_SIMPLEX, 0.65, colour, 2);
}

// ============================================================
// VISUALISATION THREAD
//
// Composite layout:
//
//   [ Camera A (normal) | Camera B (vertically flipped) ]
//
// The overlap rows are cross-blended so both images are
// simultaneously visible in the shared physical zone.
//
// Detection boxes for tag 0 and tag 1 are drawn on whichever
// camera pane sees the tag.
// ============================================================

void visThread()
{
    // Window is two panes wide
    cv::namedWindow("Tracking", cv::WINDOW_NORMAL);
    cv::resizeWindow("Tracking", PANE_W * 2, PANE_H);

    while (running)
    {
        // --- Grab frames ---
        std::array<cv::Mat, 2> raw;
        for (int c = 0; c < 2; ++c)
        {
            std::lock_guard<std::mutex> lk(cameras[c].frameMutex);
            if (cameras[c].latestFrame.empty())
                raw[c] = cv::Mat::zeros(CAM_H, CAM_W, CV_8UC3);
            else
                raw[c] = cameras[c].latestFrame.clone();
        }

        // --- Scale to display pane size ---
        std::array<cv::Mat, 2> pane;
        for (int c = 0; c < 2; ++c)
            cv::resize(raw[c], pane[c], cv::Size(PANE_W, PANE_H));

        // --- Flip Camera B vertically so its overlap (bottom) ---
        //     becomes the TOP of pane[1], aligning with pane[0] bottom
        cv::flip(pane[1], pane[1], 0);   // 0 = flip around X axis = vertical flip

        // --- Grab observations (already scaled by drawDetectionBox) ---
        TagState  s0[2], s1[2];
        DetectionBox b0[2], b1[2];
        for (int c = 0; c < 2; ++c)
        {
            std::lock_guard<std::mutex> lk(cameras[c].obsMutex);
            s0[c] = cameras[c].tag0obs;  b0[c] = cameras[c].box0obs;
            s1[c] = cameras[c].tag1obs;  b1[c] = cameras[c].box1obs;
        }

        // NOTE: For camera B the frame was flipped, so corner Y coords must
        // be mirrored: y_display = (PANE_H - 1) - y_scaled
        // We handle this by building a mirrored copy of the box.
        auto mirrorBox = [&](const DetectionBox& box) -> DetectionBox {
            DetectionBox m = box;
            if (!box.valid) return m;
            const float sy = (float)PANE_H / CAM_H;
            for (int k = 0; k < 4; ++k)
            {
                // corners are still in full-res coords here;
                // drawDetectionBox will scale them. We just flip Y.
                m.corners[k].y = CAM_H - 1.0f - box.corners[k].y;
            }
            return m;
        };

        // --- Draw detection boxes on each pane ---
        //  Cam A: normal coords
        drawDetectionBox(pane[0], b0[0], s0[0], "T0", {0, 255, 80});
        drawDetectionBox(pane[0], b1[0], s1[0], "T1", {80, 140, 255});
        //  Cam B: Y-mirrored coords (because pane was flipped)
        drawDetectionBox(pane[1], mirrorBox(b0[1]), s0[1], "T0", {0, 255, 80});
        drawDetectionBox(pane[1], mirrorBox(b1[1]), s1[1], "T1", {80, 140, 255});

        // --- Per-camera HUD overlay ---
        for (int c = 0; c < 2; ++c)
        {
            cv::Mat& f = pane[c];
            cv::putText(f, std::string("CAM ") + (char)('A'+c),
                        {16, 36}, cv::FONT_HERSHEY_SIMPLEX, 1.2, {200,200,200}, 2);
            bool cal = cameras[c].calibrated.load();
            cv::putText(f, cal ? "CAL OK" : "CALIBRATING",
                        {16, 72}, cv::FONT_HERSHEY_SIMPLEX, 0.85,
                        cal ? cv::Scalar(0,210,70) : cv::Scalar(0,70,210), 2);
        }

        // --- Blend overlap zone ---
        //  Bottom OVERLAP_PX rows of pane[0]  ←→  Top OVERLAP_PX rows of pane[1]
        //  We create the composite then blend those rows.
        cv::Mat composite;
        cv::hconcat(pane[0], pane[1], composite);

        {
            // Overlap in pane[0]: rows [PANE_H-OVERLAP_PX .. PANE_H-1]
            //            pane[1]: rows [0 .. OVERLAP_PX-1]
            // In the composite pane[0] occupies columns [0..PANE_W-1]
            // and pane[1] occupies columns [PANE_W..2*PANE_W-1].
            //
            // We draw a translucent copy of each pane's overlap strip
            // on top of the other pane's corresponding strip.

            const int y0_start = PANE_H - OVERLAP_PX;  // pane A overlap start row
            const int y1_start = 0;                      // pane B overlap start row (after flip)

            // Region of pane A bottom strip
            cv::Rect roiA_src(0,          y0_start, PANE_W, OVERLAP_PX);
            // Region of pane B top strip (in composite that's offset by PANE_W)
            cv::Rect roiB_src(PANE_W,     y1_start, PANE_W, OVERLAP_PX);

            // We blend: overlay A's bottom onto B's top and vice-versa.
            // Alpha ramp: at the edge of each camera's frame alpha=0.5 (50/50),
            // fading smoothly to 0 as we move toward the pane centre.
            cv::Mat stripA = composite(roiA_src).clone();
            cv::Mat stripB = composite(roiB_src).clone();

            // Build a vertical alpha map that goes 0.5 → 0 top-to-bottom for A
            // and 0 → 0.5 bottom-to-top for B.
            for (int row = 0; row < OVERLAP_PX; ++row)
            {
                // A fades out as we go deeper into its overlap zone (toward its center)
                double alphaA = 0.5 * (1.0 - (double)row / OVERLAP_PX);
                // B fades out toward its center (row 0 of pane B = edge of overlap)
                double alphaB = 0.5 * ((double)row / OVERLAP_PX);

                // Blend A-strip row onto B region:
                cv::addWeighted(
                    composite(cv::Rect(PANE_W, y1_start+row, PANE_W, 1)),
                    1.0 - alphaA,
                    stripA(cv::Rect(0, row, PANE_W, 1)),
                    alphaA, 0.0,
                    composite(cv::Rect(PANE_W, y1_start+row, PANE_W, 1)));

                // Blend B-strip row onto A region:
                cv::addWeighted(
                    composite(cv::Rect(0, y0_start+row, PANE_W, 1)),
                    1.0 - alphaB,
                    stripB(cv::Rect(0, row, PANE_W, 1)),
                    alphaB, 0.0,
                    composite(cv::Rect(0, y0_start+row, PANE_W, 1)));
            }

            // Draw a thin line at the blend boundary for reference
            cv::line(composite,
                     {PANE_W-1, 0}, {PANE_W-1, PANE_H},
                     {80, 80, 80}, 1);
        }

        // --- Fused pose overlay (bottom-left of composite) ---
        {
            TagState sf0, sf1;
            { std::lock_guard<std::mutex> lk(fusedMutex); sf0=fusedTag0; sf1=fusedTag1; }

            auto fusedLine = [&](const TagState& ts, const std::string& lbl,
                                 int y, cv::Scalar col)
            {
                std::ostringstream oss;
                oss << "[FUSED] " << lbl;
                if (ts.visible)
                    oss << " x="   << std::fixed<<std::setprecision(3) << ts.pose.x
                        << " y="   << ts.pose.y
                        << " yaw=" << std::setprecision(1) << ts.pose.yaw
                        << " cf="  << std::setprecision(2) << ts.confidence;
                else oss << " [lost]";
                cv::putText(composite, oss.str(), {16, y},
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, col, 2);
            };

            fusedLine(sf0, "T0", PANE_H-50, {0, 255, 130});
            fusedLine(sf1, "T1", PANE_H-20, {0, 130, 255});
        }

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
        if (std::strcmp(argv[i], "--no-vis") == 0) enableVis = false;

    cameras[0].id = 0; cameras[0].device = CAM_DEVICE_A;
    cameras[1].id = 1; cameras[1].device = CAM_DEVICE_B;
    for (auto& c : cameras) { c.K = makeK(); c.D = makeD(); }

    // Start pipelines
    for (int c = 0; c < 2; ++c)
    {
        GError* gerr = nullptr;
        std::string ps = buildPipeline(cameras[c].device);
        cameras[c].pipeline = gst_parse_launch(ps.c_str(), &gerr);
        if (!cameras[c].pipeline || gerr)
        {
            std::cerr << "[ERROR] Cam " << c << ": "
                      << (gerr ? gerr->message : "unknown") << "\n";
            return 1;
        }
        cameras[c].sink =
            gst_bin_get_by_name(GST_BIN(cameras[c].pipeline), "sink");
        gst_element_set_state(cameras[c].pipeline, GST_STATE_PLAYING);
        std::cerr << "[INFO] Camera " << c
                  << " started (" << cameras[c].device
                  << ")  " << CAM_W << "×" << CAM_H << "\n";
    }

    std::thread capA  (captureThread,  std::ref(cameras[0]));
    std::thread capB  (captureThread,  std::ref(cameras[1]));
    std::thread trkA  (trackingThread, std::ref(cameras[0]));
    std::thread trkB  (trackingThread, std::ref(cameras[1]));
    std::thread fusion(fusionThread);
    std::thread vis;
    if (enableVis)
        vis = std::thread(visThread);
    else
        std::cerr << "[INFO] Visualization disabled (--no-vis)\n";

    capA.join(); capB.join();
    trkA.join(); trkB.join();
    fusion.join();
    if (vis.joinable()) vis.join();

    for (int c = 0; c < 2; ++c)
    {
        gst_element_set_state(cameras[c].pipeline, GST_STATE_NULL);
        gst_object_unref(cameras[c].pipeline);
    }
    return 0;
}
