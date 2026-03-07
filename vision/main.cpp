#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cmath>

#include <opencv2/opencv.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

// ============================================================
// Config
// ============================================================
constexpr int resoluion_divider = 2; // 1 full res   2 half res...
constexpr int LOW_W = 640;
constexpr int LOW_H = 480;
constexpr int ROI_PAD = 80;
constexpr double TAG_SIZE = 0.1552;

// ============================================================
// Shared data structs
// ============================================================
struct FrameData {
    std::shared_ptr<cv::Mat> frame;
    int64_t t_cam_ns;
};

struct ROIResult {
    FrameData data;
    cv::Rect roi;
    bool valid = false;
};

struct PoseResult {
    FrameData data;
    cv::Vec3d tvec;
    double yaw = 0;
    cv::Rect roi;
    bool valid = false;
};

// ============================================================
// Globals (simple & explicit)
// ============================================================
std::mutex m_cap, m_low, m_high, m_vis;
std::condition_variable cv_cap, cv_low, cv_high, cv_vis;

std::shared_ptr<FrameData> cap_frame;
std::shared_ptr<ROIResult> roi_result;
std::shared_ptr<PoseResult> pose_result;

std::atomic<bool> running{true};

// ============================================================
// Imshow helper
// ============================================================
void imshowScaled(const std::string& name, const cv::Mat& img,
                  int maxW = 1600, int maxH = 900)
{
    double scale = std::min(
        maxW / double(img.cols),
        maxH / double(img.rows)
    );
    if (scale < 1.0) {
        cv::Mat r;
        cv::resize(img, r, {}, scale, scale);
        cv::imshow(name, r);
    } else {
        cv::imshow(name, img);
    }
}

// ============================================================
// Capture Thread
// ============================================================
void captureThread(GstElement* sink)
{
    while (running) {
        GstSample* sample =
            gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 100000000);
        if (!sample) continue;

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        GstCaps* caps = gst_sample_get_caps(sample);
        GstStructure* s = gst_caps_get_structure(caps, 0);
        int w, h;
        gst_structure_get_int(s, "width", &w);
        gst_structure_get_int(s, "height", &h);

        auto fd = std::make_shared<FrameData>();
        fd->t_cam_ns = GST_BUFFER_PTS(buffer);
        fd->frame = std::make_shared<cv::Mat>(
            h, w, CV_8UC3, map.data);
        fd->frame = std::make_shared<cv::Mat>(fd->frame->clone());

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);

        {
            std::lock_guard<std::mutex> lk(m_cap);
            cap_frame = fd;
        }
        cv_cap.notify_one();
    }
}

// ============================================================
// Low-Res Thread
// ============================================================
void lowResThread()
{
    apriltag_family_t* tf = tag36h11_create();
    apriltag_detector_t* td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = 1.0;
    td->nthreads = 1;

    while (running) {
        std::unique_lock<std::mutex> lk(m_cap);
        cv_cap.wait(lk, []{ return cap_frame || !running; });
        if (!running) break;

        auto fd = cap_frame;
        cap_frame.reset();
        lk.unlock();

        cv::Mat gray, low;
        cv::cvtColor(*fd->frame, gray, cv::COLOR_BGR2GRAY);
        cv::resize(gray, low, {LOW_W, LOW_H});

        image_u8_t img {
            low.cols, low.rows, int(low.step), low.data
        };

        zarray_t* dets = apriltag_detector_detect(td, &img);
        if (zarray_size(dets) == 0) {
            apriltag_detections_destroy(dets);
            continue;
        }

        apriltag_detection_t* det;
        zarray_get(dets, 0, &det);

        double minx=1e9,miny=1e9,maxx=-1e9,maxy=-1e9;
        for (int i=0;i<4;i++) {
            minx = std::min(minx, det->p[i][0]);
            miny = std::min(miny, det->p[i][1]);
            maxx = std::max(maxx, det->p[i][0]);
            maxy = std::max(maxy, det->p[i][1]);
        }

        float sx = float(gray.cols)/LOW_W;
        float sy = float(gray.rows)/LOW_H;

        auto rr = std::make_shared<ROIResult>();
        rr->data = *fd;
        rr->roi = cv::Rect(
            int(minx*sx)-ROI_PAD,
            int(miny*sy)-ROI_PAD,
            int((maxx-minx)*sx)+2*ROI_PAD,
            int((maxy-miny)*sy)+2*ROI_PAD
        ) & cv::Rect(0,0,gray.cols,gray.rows);
        rr->valid = true;

        {
            std::lock_guard<std::mutex> lk2(m_low);
            roi_result = rr;
        }
        cv_low.notify_one();

        apriltag_detections_destroy(dets);
    }

    apriltag_detector_destroy(td);
    tag36h11_destroy(tf);
}

// ============================================================
// High-Res Thread
// ============================================================
void highResThread(const cv::Mat& K, const cv::Mat& D)
{
    apriltag_family_t* tf = tag36h11_create();
    apriltag_detector_t* td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = 3.0;

    while (running) {
        std::unique_lock<std::mutex> lk(m_low);
        cv_low.wait(lk, []{ return roi_result || !running; });
        if (!running) break;

        auto rr = roi_result;
        roi_result.reset();
        lk.unlock();

        if (!rr->valid) continue;

        cv::Mat gray;
        cv::cvtColor(*rr->data.frame, gray, cv::COLOR_BGR2GRAY);
        cv::Mat roi_gray = gray(rr->roi);

        image_u8_t img {
            roi_gray.cols, roi_gray.rows,
            int(roi_gray.step), roi_gray.data
        };

        zarray_t* dets = apriltag_detector_detect(td, &img);
        if (zarray_size(dets) == 0) {
            apriltag_detections_destroy(dets);
            continue;
        }

        apriltag_detection_t* det;
        zarray_get(dets, 0, &det);

        std::vector<cv::Point2f> img_pts;
        for (int i=0;i<4;i++)
            img_pts.emplace_back(
                det->p[i][0]+rr->roi.x,
                det->p[i][1]+rr->roi.y
            );

        std::vector<cv::Point3f> obj_pts = {
            {-TAG_SIZE/2,-TAG_SIZE/2,0},
            { TAG_SIZE/2,-TAG_SIZE/2,0},
            { TAG_SIZE/2, TAG_SIZE/2,0},
            {-TAG_SIZE/2, TAG_SIZE/2,0}
        };

        cv::Mat rvec, tvec;
        cv::solvePnP(obj_pts, img_pts, K, D, rvec, tvec);

        cv::Mat R;
        cv::Rodrigues(rvec, R);
        double yaw = atan2(R.at<double>(1,0), R.at<double>(0,0))
                     * 180.0 / CV_PI;

        auto pr = std::make_shared<PoseResult>();
        pr->data = rr->data;
        pr->tvec = tvec;
        pr->yaw = yaw;
        pr->roi = rr->roi;
        pr->valid = true;

        {
            std::lock_guard<std::mutex> lk2(m_high);
            pose_result = pr;
        }
        cv_high.notify_one();

        apriltag_detections_destroy(dets);
    }

    apriltag_detector_destroy(td);
    tag36h11_destroy(tf);
}

// ============================================================
// Visualization Thread
// ============================================================
void visThread()
{
    while (running) {
        std::unique_lock<std::mutex> lk(m_high);
        cv_high.wait(lk, []{ return pose_result || !running; });
        if (!running) break;

        auto pr = pose_result;
        pose_result.reset();
        lk.unlock();

        if (!pr->valid) continue;

        cv::Mat vis = pr->data.frame->clone();
        cv::rectangle(vis, pr->roi, {255,0,0}, 2);

        char txt[128];
        snprintf(txt, sizeof(txt),
                 "X=%.2f Y=%.2f Yaw=%.1f",
                 pr->tvec[0], pr->tvec[1], pr->yaw);

        cv::putText(vis, txt, {30,40},
                    cv::FONT_HERSHEY_SIMPLEX, 1.2,
                    {0,255,0}, 2);

        imshowScaled("AprilTag Multicore", vis);

        if (cv::waitKey(1) == 'q')
            running = false;
    }
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char** argv)
{
    gst_init(&argc, &argv);

    std::string pipeline =
        "libcamerasrc "
        "exposure-time-mode=manual exposure-time=1500 " "analogue-gain-mode=manual analogue-gain=15 "
        "! video/x-raw,width=(4056/resoluion_divider),height=(3040/resoluion_divider),format=BGRx "
        "! videoconvert "
        "! video/x-raw,format=BGR "
        "! appsink name=sink drop=true max-buffers=1";

    GstElement* pipe = gst_parse_launch(pipeline.c_str(), nullptr);
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipe), "sink");
    gst_element_set_state(pipe, GST_STATE_PLAYING);

    cv::Mat K = (cv::Mat1d(3,3) <<
        (4009.22661/resoluion_divider),0,(2113.49677/resoluion_divider),
        0,(4020.48344/resoluion_divider),(1469.08894/resoluion_divider),
        0,0,1);
        
    cv::Mat D = (cv::Mat1d(1,5) <<
        -0.49106571,0.283421,0.00061827,-0.00242921,-0.09694459);

    std::thread t_cap(captureThread, sink);
    std::thread t_low(lowResThread);
    std::thread t_high(highResThread, K, D);
    std::thread t_vis(visThread);

    t_cap.join();
    t_low.join();
    t_high.join();
    t_vis.join();

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return 0;
}
