// ============================================================
//  Dual-camera AprilTag tracker
//
//  Changes:
//   - Visualisation shows only raw frames; no text/overlay.
//   - Both frames are rotated 90° so their long (bottom) sides
//     face each other: left frame rotated CW, right frame CCW,
//     then the pair is placed side by side.
//   - Each major task (capture A/B, tracking A/B, fusion,
//     visualisation) is pinned to its own CPU core to keep
//     the vis thread from stalling behind the detector.
//   - Frame hand-off uses a condition_variable so tracking
//     threads sleep instead of busy-spinning.
//   - A lightweight ring-buffer (depth=2) separates capture
//     from tracking; the tracking thread always gets the newest
//     frame and never blocks the capture thread.
//   - The vis thread pulls frames with a short try-lock; it
//     shows the last known good frame if the camera is briefly
//     unavailable, preventing blank / frozen windows.
//   - Tracking threads enforce a minimum inter-frame gap
//     (TRACK_MIN_INTERVAL_MS) so the AprilTag detector cannot
//     monopolise a core at 100 % when frames arrive faster
//     than it can process them.
//   - Stale-frame detection threshold reduced to 300 ms and
//     the staleness check is non-blocking (atomic timestamp).
// ============================================================

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <vector>
#include <array>
#include <string>
#include <sstream>
#include <iomanip>
<<<<<<< Updated upstream
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

#include <pthread.h>
#include <sched.h>

#include <arpa/inet.h>
#include <netinet/in.h>
=======
#include <string>

#include <arpa/inet.h>
>>>>>>> Stashed changes
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

constexpr int    RESOLUTION_DIVIDER      = 2;

// Physical display size for each rotated camera pane.
// After 90° rotation the frame dims swap: what was W becomes H.
constexpr int    DISPLAY_W               = 760;   // was DISPLAY_H
constexpr int    DISPLAY_H               = 1014;  // was DISPLAY_W

// Minimum ms between successive detector runs (per camera).
// Keeps a single tracking thread from pegging a core at 100 %.
constexpr int    TRACK_MIN_INTERVAL_MS   = 20;    // ≤ 50 Hz

// AprilTag physical sizes (metres)
constexpr double TAG_SIZE                = 0.10;
constexpr double CALIB_TAG_SIZE          = 0.10;

// Calibration rectangle (metres)
constexpr double CALIB_W                 = 1.60;
constexpr double CALIB_H                 = 0.60;

// Tracking tag IDs (TRACK_TAG1 is overridable at runtime via --single-tag)
constexpr int    TRACK_TAG0         = 0;
int              TRACK_TAG1         = 1;
bool             g_singleTagMode    = false;

// Confidence weights
constexpr double W_MARGIN                = 0.50;
constexpr double W_HAMMING               = 0.25;
constexpr double W_REPROJ                = 0.15;
constexpr double W_SIZE                  = 0.10;

constexpr double MARGIN_SAT              = 80.0;
constexpr double SIZE_SAT                = 200.0;
constexpr double REPROJ_MAX              = 5.0;

// Stale frame threshold
constexpr uint64_t STALE_NS              = 300ULL * 1'000'000ULL; // 300 ms

const std::string CAM_DEVICE_A = "/base/axi/pcie@1000120000/rp1/i2c@88000/imx477@1a";
const std::string CAM_DEVICE_B = "/base/axi/pcie@1000120000/rp1/i2c@80000/imx477@1a";

constexpr const char* DETECTOR_UDP_HOST = "127.0.0.1";
constexpr uint16_t    DETECTOR_UDP_PORT = 9001;

// ============================================================
// CPU CORE ASSIGNMENTS  (adjust if your Pi has fewer cores)
// Core 0 left for the OS / GStreamer demux internals.
// ============================================================
constexpr int CORE_CAPTURE_A  = 1;
constexpr int CORE_CAPTURE_B  = 2;
constexpr int CORE_TRACK_A    = 3;
constexpr int CORE_TRACK_B    = 4;
constexpr int CORE_FUSION     = 5;
constexpr int CORE_VIS        = 6;   // safe even on 4-core: clamped below

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
// CALIBRATION RECTANGLE CORNERS
// ============================================================

static std::map<int, std::vector<cv::Point3f>> buildWorldTagCorners()
{
    const double W = CALIB_W, H = CALIB_H, S = CALIB_TAG_SIZE;
    return {
        {2, { {(float)(-W/2),   (float)( H/2),   0}, {(float)(-W/2+S), (float)( H/2),   0},
              {(float)(-W/2+S), (float)( H/2-S), 0}, {(float)(-W/2),   (float)( H/2-S), 0} }},
        {3, { {(float)( W/2-S), (float)( H/2),   0}, {(float)( W/2),   (float)( H/2),   0},
              {(float)( W/2),   (float)( H/2-S), 0}, {(float)( W/2-S), (float)( H/2-S), 0} }},
        {4, { {(float)( W/2-S), (float)(-H/2+S), 0}, {(float)( W/2),   (float)(-H/2+S), 0},
              {(float)( W/2),   (float)(-H/2),   0}, {(float)( W/2-S), (float)(-H/2),   0} }},
        {5, { {(float)(-W/2),   (float)(-H/2+S), 0}, {(float)(-W/2+S), (float)(-H/2+S), 0},
              {(float)(-W/2+S), (float)(-H/2),   0}, {(float)(-W/2),   (float)(-H/2),   0} }}
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

// ============================================================
// FRAME SLOT  —  single-producer / single-consumer ring buffer
// depth=2: capture writes slot[writeIdx], tracking reads slot[readIdx].
// ============================================================

struct FrameSlot
{
    cv::Mat          frame;
    uint64_t         tsNs = 0;
};

struct FrameRing
{
    static constexpr int DEPTH = 2;
    FrameSlot        slots[DEPTH];
    std::atomic<int> writeIdx{0};     // capture owns this
    std::mutex       mu;
    std::condition_variable cv;
    std::atomic<int> generation{0};   // incremented on every new write
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

    FrameRing   ring;

    cv::Mat     K, D;

    cv::Mat     R_wc, t_wc;
    std::atomic<bool> calibrated{false};

    std::mutex  obsMutex;
    TagState    tag0obs, tag1obs;

    // For vis thread: last rendered frame (display resolution)
    std::mutex  visMutex;
    cv::Mat     visFrame;
};

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool>     running(true);
std::atomic<uint64_t> frameCounter(0);
std::array<CameraContext, 2> cameras;

std::mutex  fusedMutex;
TagState    fusedTag0, fusedTag1;

static const auto worldTagCorners = buildWorldTagCorners();

// ============================================================
// THREAD PINNING HELPER
// ============================================================

static void pinToCore(int core)
{
    int ncores = static_cast<int>(std::thread::hardware_concurrency());
    if (ncores < 1) ncores = 1;
    core = core % ncores;   // clamp gracefully on smaller boards

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}

// ============================================================
// HELPERS
// ============================================================

static inline double clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }

static double tagPixelDiag(apriltag_detection_t* det)
{
    double dx = det->p[2][0]-det->p[0][0], dy = det->p[2][1]-det->p[0][1];
    return std::sqrt(dx*dx+dy*dy);
}

static double reprojError(
    const std::vector<cv::Point3f>& obj, const std::vector<cv::Point2f>& img,
    const cv::Mat& rv, const cv::Mat& tv, const cv::Mat& K, const cv::Mat& D)
{
    std::vector<cv::Point2f> proj;
    cv::projectPoints(obj, rv, tv, K, D, proj);
    double e = 0;
    for (size_t i=0;i<img.size();++i){double dx=img[i].x-proj[i].x,dy=img[i].y-proj[i].y;e+=std::sqrt(dx*dx+dy*dy);}
    return e / img.size();
}

static double computeConf(
    apriltag_detection_t* det, const cv::Mat& rv, const cv::Mat& tv,
    const std::vector<cv::Point3f>& obj, const std::vector<cv::Point2f>& img,
    const cv::Mat& K, const cv::Mat& D)
{
    double sm = clamp01(det->decision_margin/MARGIN_SAT);
    double sh = det->hamming==0?1.0:det->hamming==1?0.5:0.0;
    double sr = clamp01(1.0 - reprojError(obj,img,rv,tv,K,D)/REPROJ_MAX);
    double ss = clamp01(tagPixelDiag(det)/SIZE_SAT);
    return clamp01(W_MARGIN*sm + W_HAMMING*sh + W_REPROJ*sr + W_SIZE*ss);
}

static cv::Point3f camToWorld(const cv::Mat& tv, const cv::Mat& R_wc, const cv::Mat& t_wc)
{
    if (R_wc.empty()||t_wc.empty()) return {};
    cv::Mat p = R_wc*tv + t_wc;
    return {(float)p.at<double>(0),(float)p.at<double>(1),(float)p.at<double>(2)};
}

static apriltag_detector_t* createDetector()
{
    auto tf = tag36h11_create();
    auto td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = 2.0;
    td->nthreads      = 2;   // reduced: each tracking thread gets 2 AT threads
    td->refine_edges  = 1;
    return td;
}

// ============================================================
// GSTREAMER PIPELINE
// ============================================================

static std::string buildPipeline(const std::string& device)
{
    return
        "libcamerasrc camera-name=" + device + " ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink name=sink max-buffers=1 drop=true sync=false";
}

// ============================================================
// CAPTURE THREAD  — writes frames into the ring buffer
// ============================================================

void captureThread(CameraContext& cam, int core)
{
    pinToCore(core);
    while (running)
    {
        auto sample = gst_app_sink_try_pull_sample(
            GST_APP_SINK(cam.sink), 200*GST_MSECOND);
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

        const uint64_t nowNs =
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

        // Write into next slot (round-robin, never blocks tracking)
        int wi = (cam.ring.writeIdx.load(std::memory_order_relaxed)+1) % FrameRing::DEPTH;
        {
            // No lock on the slot itself — tracking only reads the previous slot
            cam.ring.slots[wi].frame = frame.clone();
            cam.ring.slots[wi].tsNs  = nowNs;
            cam.ring.writeIdx.store(wi, std::memory_order_release);
        }

        // Wake tracking thread
        {
            std::lock_guard<std::mutex> lk(cam.ring.mu);
            cam.ring.generation.fetch_add(1, std::memory_order_relaxed);
        }
        cam.ring.cv.notify_one();

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
    }
}

// ============================================================
// CALIBRATION
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
// TRACKING THREAD  — reads frames, runs detector
// ============================================================

<<<<<<< Updated upstream
void trackingThread(CameraContext& cam, int core)
=======
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

static std::string detectorContractJson(
    uint64_t frame_id,
    const TagState& satellite,
    const TagState& end_mass)
{
    // Orbital angle of end mass around satellite in [0, 2pi).
    const double dx = end_mass.pose.x - satellite.pose.x;
    const double dy = end_mass.pose.y - satellite.pose.y;
    double orbital = std::atan2(dy, dx);
    if (orbital < 0.0) orbital += 2.0 * CV_PI;

    const double conf =
        (satellite.visible && end_mass.visible)
            ? 0.5 * (satellite.confidence + end_mass.confidence)
            : 0.0;

    auto ts_s = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    return "{"
        "\"timestamp\":" + fp(ts_s, 6) +
        ",\"frame_id\":" + std::to_string(frame_id) +
        ",\"camera_id\":\"cam0\""
        ",\"satellite_position\":{\"x\":" + fp(satellite.pose.x) + ",\"y\":" + fp(satellite.pose.y) + "}"
        ",\"end_mass_position\":{\"x\":" + fp(end_mass.pose.x) + ",\"y\":" + fp(end_mass.pose.y) + "}"
        ",\"orbital_angular_position\":" + fp(orbital, 6) +
        ",\"tracking_confidence\":" + fp(conf, 3) +
        "}";
}

// ============================================================
// TRACKING THREAD
// ============================================================

void trackingThread()
>>>>>>> Stashed changes
{
    pinToCore(core);
    auto detector = createDetector();
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(9001);
    udp_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const std::vector<cv::Point3f> trackObj =
    {
        {-(float)TAG_SIZE/2, -(float)TAG_SIZE/2, 0},
        { (float)TAG_SIZE/2, -(float)TAG_SIZE/2, 0},
        { (float)TAG_SIZE/2,  (float)TAG_SIZE/2, 0},
        {-(float)TAG_SIZE/2,  (float)TAG_SIZE/2, 0}
    };

    int lastGen = -1;
    auto lastRunTime = std::chrono::steady_clock::now();

    while (running)
    {
        // Wait for a new frame (with timeout so we can check running)
        {
            std::unique_lock<std::mutex> lk(cam.ring.mu);
            cam.ring.cv.wait_for(lk, std::chrono::milliseconds(100),
                [&]{ return !running ||
                     cam.ring.generation.load(std::memory_order_relaxed) != lastGen; });
        }
        if (!running) break;

        int gen = cam.ring.generation.load(std::memory_order_acquire);
        if (gen == lastGen) continue;  // spurious wakeup
        lastGen = gen;

        // Enforce minimum inter-frame gap
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - lastRunTime).count();
        if (elapsedMs < TRACK_MIN_INTERVAL_MS)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(TRACK_MIN_INTERVAL_MS - elapsedMs));
        }
        lastRunTime = std::chrono::steady_clock::now();

        // Read latest frame
        int ri = cam.ring.writeIdx.load(std::memory_order_acquire);
        const FrameSlot& slot = cam.ring.slots[ri];
        if (slot.frame.empty()) continue;

        cv::Mat frame = slot.frame.clone();
        uint64_t frameTsNs = slot.tsNs;

        // Stale check
        const uint64_t nowNs =
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        if (frameTsNs == 0 || nowNs - frameTsNs > STALE_NS)
        {
            TagState lost;
            {
                std::lock_guard<std::mutex> lock(cam.obsMutex);
                cam.tag0obs = lost;
                cam.tag1obs = lost;
            }
            continue;
        }

        ++frameCounter;

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        image_u8_t img { gray.cols, gray.rows, gray.cols, gray.data };
        auto detections = apriltag_detector_detect(detector, &img);

        // Debug: dump every detected tag ID per camera per frame, along with
        // hamming (bit errors) and decision_margin (decode quality). Anything
        // with hamming > 1 is dropped at the filter below; that would explain
        // a detection appearing here but never reaching tag1obs.
        // Grep for it: tail -f logs/vision.log | grep '\[cam'
        if (zarray_size(detections) > 0)
        {
            std::cerr << "[cam " << cam.id << "] detections="
                      << zarray_size(detections);
            for (int i = 0; i < zarray_size(detections); ++i)
            {
                apriltag_detection_t* d;
                zarray_get(detections, i, &d);
                std::cerr << " id=" << d->id
                          << "(h=" << d->hamming
                          << ",m=" << d->decision_margin << ")";
                if (d->hamming > 1) std::cerr << "[DROPPED]";
            }
            std::cerr << "\n";
        }

        std::vector<cv::Point2f> calibImg;
        std::vector<cv::Point3f> calibObj;
        TagState new0, new1;

        for (int i = 0; i < zarray_size(detections); ++i)
        {
            apriltag_detection_t* det;
            zarray_get(detections, i, &det);
            if (det->hamming > 1) continue;

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
                for (int k=0;k<4;++k) imgPts.emplace_back(det->p[k][0],det->p[k][1]);

                cv::Mat rvec, tvec;
                cv::solvePnP(trackObj, imgPts, cam.K, cam.D, rvec, tvec);

                double conf = computeConf(det,rvec,tvec,trackObj,imgPts,cam.K,cam.D);
                auto world  = camToWorld(tvec, cam.R_wc, cam.t_wc);

                cv::Mat R; cv::Rodrigues(rvec, R);
                double yaw = std::atan2(R.at<double>(1,0),R.at<double>(0,0))*180.0/CV_PI;

                TagState ts;
                ts.pose       = {world.x, world.y, yaw};
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

        // Push display-resolution frame (rotated) to vis buffer
        // Camera A rotated 90° CW  (ROTATE_90_CLOCKWISE)
        // Camera B rotated 90° CCW (ROTATE_90_COUNTERCLOCKWISE)
        // This brings both "bottom long sides" to face the centre seam.
        {
            cv::Mat rotated;
            cv::rotate(frame,
                       rotated,
                       cam.id == 0 ? cv::ROTATE_90_CLOCKWISE
                                   : cv::ROTATE_90_COUNTERCLOCKWISE);
            cv::Mat display;
            cv::resize(rotated, display, cv::Size(DISPLAY_W, DISPLAY_H));

            std::lock_guard<std::mutex> lk(cam.visMutex);
            cam.visFrame = std::move(display);
        }

        apriltag_detections_destroy(detections);
<<<<<<< Updated upstream
=======

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

        if (udp_sock >= 0)
        {
            const std::string msg = detectorContractJson(frameCounter.load(), s0, s1);
            sendto(udp_sock, msg.c_str(), msg.size(), 0,
                   reinterpret_cast<sockaddr*>(&udp_addr), sizeof(udp_addr));
        }
>>>>>>> Stashed changes
    }

    if (udp_sock >= 0) close(udp_sock);
}

// ============================================================
// FUSION + JSON OUTPUT THREAD
// ============================================================

void fusionThread(int core)
{
    pinToCore(core);

    int udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in udpAddr{};
    if (udpFd >= 0)
    {
        udpAddr.sin_family = AF_INET;
        udpAddr.sin_port   = htons(DETECTOR_UDP_PORT);
        if (inet_pton(AF_INET, DETECTOR_UDP_HOST, &udpAddr.sin_addr) != 1)
        {
            close(udpFd); udpFd = -1;
        }
    }
    if (udpFd < 0)
        std::cerr << "[WARN] detector UDP socket unavailable\n";

    while (running)
    {
        TagState obs[2][2];
        for (int c=0;c<2;++c)
        {
            std::lock_guard<std::mutex> lock(cameras[c].obsMutex);
            obs[c][0] = cameras[c].tag0obs;
            obs[c][1] = cameras[c].tag1obs;
        }

        auto fuse = [&](int ti) -> TagState
        {
            const TagState& a=obs[0][ti]; const TagState& b=obs[1][ti];
            if ( a.visible && !b.visible) return a;
            if (!a.visible &&  b.visible) return b;
            if (!a.visible && !b.visible) return {};
            return (a.confidence >= b.confidence) ? a : b;
        };

        TagState f0=fuse(0), f1=fuse(1);
        {
            std::lock_guard<std::mutex> lock(fusedMutex);
            fusedTag0=f0; fusedTag1=f1;
        }

        // --- Detector contract -> Python (UDP), docs/api.md ---
        const bool gateOpen = g_singleTagMode
            ? f1.visible
            : (f0.visible && f1.visible);
        if (udpFd >= 0 && gateOpen)
        {
            const auto wallNow = std::chrono::system_clock::now();
            const double tsSec = std::chrono::duration<double>(
                                     wallNow.time_since_epoch())
                                     .count();

            double satX = 0.0, satY = 0.0, orbit = 0.0;
            double trackConf = f1.confidence;
            if (!g_singleTagMode)
            {
                satX = f0.pose.x;
                satY = f0.pose.y;
                const double dx = f1.pose.x - f0.pose.x;
                const double dy = f1.pose.y - f0.pose.y;
                orbit = std::atan2(dy, dx);
                if (orbit < 0.0)
                    orbit += 2.0 * std::acos(-1.0);
                trackConf = std::min(f0.confidence, f1.confidence);
            }

            std::ostringstream det;
            det.setf(std::ios::fixed);
            det << std::setprecision(6)
                << "{\"timestamp\":"<<tsSec<<",\"frame_id\":"<<frameCounter.load()
                << ",\"camera_id\":\"fused\""
                << ",\"satellite_position\":{\"x\":" << satX
                << ",\"y\":" << satY << '}'
                << ",\"end_mass_position\":{\"x\":" << f1.pose.x
                << ",\"y\":" << f1.pose.y << '}'
                << ",\"orbital_angular_position\":" << orbit
                << ",\"tracking_confidence\":";
            det << std::setprecision(4) << trackConf << '}';

            const std::string s=det.str();
            (void)sendto(udpFd, s.data(), s.size(), 0,
                         reinterpret_cast<const sockaddr*>(&udpAddr),
                         sizeof(udpAddr));
        }

        // JSON stdout
        auto fp=[](double v,int p=4)->std::string{
            std::ostringstream o; o<<std::fixed<<std::setprecision(p)<<v; return o.str();};
        auto cd=[&](const TagState& ts)->std::string{
            return "{\"conf\":"+fp(ts.confidence,3)+",\"visible\":"+(ts.visible?"true":"false")+"}";}; 
        auto tj=[&](const std::string& k,const TagState& f,int ti)->std::string{
            return "\""+k+"\":{\"x\":"+fp(f.pose.x)+",\"y\":"+fp(f.pose.y)
                  +",\"yaw\":"+fp(f.pose.yaw)+",\"conf\":"+fp(f.confidence,3)
                  +",\"visible\":"+(f.visible?"true":"false")
                  +",\"camA\":"+cd(obs[0][ti])+",\"camB\":"+cd(obs[1][ti])+"}";}; 

        auto ts_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        std::cout
            <<"{"<<"\"ts\":"<<ts_ns<<",\"frame\":"<<frameCounter.load()
            <<",\"calA\":"<<(cameras[0].calibrated?"true":"false")
            <<",\"calB\":"<<(cameras[1].calibrated?"true":"false")
            <<","<<tj("tag0",f0,0)<<","<<tj("tag1",f1,1)<<"}\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (udpFd>=0) close(udpFd);
}

// ============================================================
// VISUALISATION THREAD  — no overlays, just raw rotated frames
//
// Layout after rotation:
//   Left pane  = Cam A rotated 90° CW   → bottom (long side) faces right/centre
//   Right pane = Cam B rotated 90° CCW  → bottom (long side) faces left/centre
//
// The two panes are placed side-by-side so both bottom long
// edges touch at the vertical seam in the middle.
// ============================================================

void visThread(int core)
{
    pinToCore(core);
    cv::namedWindow("Tracking", cv::WINDOW_NORMAL);
    cv::resizeWindow("Tracking", DISPLAY_W * 2, DISPLAY_H);

    // Keep the last good frame for each camera; avoids black flash on stall
    cv::Mat lastGood[2] = {
        cv::Mat::zeros(DISPLAY_H, DISPLAY_W, CV_8UC3),
        cv::Mat::zeros(DISPLAY_H, DISPLAY_W, CV_8UC3)
    };

    while (running)
    {
        for (int c=0; c<2; ++c)
        {
            // try_lock: skip this camera if tracking thread holds the lock
            std::unique_lock<std::mutex> lk(cameras[c].visMutex, std::try_to_lock);
            if (lk.owns_lock() && !cameras[c].visFrame.empty())
                lastGood[c] = cameras[c].visFrame.clone();
        }

        cv::Mat composite;
        cv::hconcat(lastGood[0], lastGood[1], composite);
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
        else if (std::strcmp(argv[i], "--single-tag") == 0 && i + 1 < argc)
        {
            TRACK_TAG1      = std::atoi(argv[++i]);
            g_singleTagMode = true;
        }
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

    if (g_singleTagMode)
    {
        for (int c = 0; c < 2; ++c)
        {
            cameras[c].R_wc = (cv::Mat_<double>(3, 3) <<
                                1.0,  0.0, 0.0,
                                0.0, -1.0, 0.0,
                                0.0,  0.0, 1.0);
            cameras[c].t_wc = cv::Mat::zeros(3, 1, CV_64F);
            cameras[c].calibrated = true;
        }
    }

    for (int c=0;c<2;++c)
    {
        GError* gerr=nullptr;
        std::string pipeStr=buildPipeline(cameras[c].device);
        cameras[c].pipeline=gst_parse_launch(pipeStr.c_str(),&gerr);
        if (!cameras[c].pipeline||gerr)
        {
            std::cerr<<"[ERROR] Camera "<<c<<" pipeline: "
                     <<(gerr?gerr->message:"unknown")<<"\n";
            return 1;
        }
        cameras[c].sink=gst_bin_get_by_name(GST_BIN(cameras[c].pipeline),"sink");
        gst_element_set_state(cameras[c].pipeline,GST_STATE_PLAYING);
        std::cerr<<"[INFO] Camera "<<c<<" started ("<<cameras[c].device<<")\n";
    }

    std::thread capA   ([](){ captureThread (cameras[0], CORE_CAPTURE_A); });
    std::thread capB   ([](){ captureThread (cameras[1], CORE_CAPTURE_B); });
    std::thread trackA ([](){ trackingThread(cameras[0], CORE_TRACK_A);   });
    std::thread trackB ([](){ trackingThread(cameras[1], CORE_TRACK_B);   });
    std::thread fusion ([](){ fusionThread  (CORE_FUSION);                });
    std::thread vis;
    if (enableVis)
        vis = std::thread([](){ visThread(CORE_VIS); });
    else
        std::cerr<<"[INFO] Visualization disabled (--no-vis)\n";

    capA.join(); capB.join();
    trackA.join(); trackB.join();
    fusion.join();
    if (vis.joinable()) vis.join();

    for (int c=0;c<2;++c)
    {
        gst_element_set_state(cameras[c].pipeline,GST_STATE_NULL);
        gst_object_unref(cameras[c].pipeline);
    }
    return 0;
}