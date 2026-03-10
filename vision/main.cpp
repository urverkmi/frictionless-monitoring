#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <map>
#include <vector>
#include <cmath>

#include <opencv2/opencv.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

// ============================================================
// CONFIG
// ============================================================

constexpr int resoluion_divider = 1; // 1 full res   2 half res...

constexpr int LOW_W = 640;
constexpr int LOW_H = 480;

constexpr int ROI_PAD = 50;

constexpr double TAG_SIZE = 0.1; //meter 0.1552
constexpr double SIDE_LENGTH = 0.8;  // calibration tag square

constexpr int TRACK_ID0 = 0;
constexpr int TRACK_ID1 = 1;

// calibration ids CCW
constexpr int CAL_ID0 = 2;
constexpr int CAL_ID1 = 3;
constexpr int CAL_ID2 = 4;
constexpr int CAL_ID3 = 5;

// ============================================================
// WORLD POINTS (CCW)
// ============================================================

std::map<int, cv::Point3f> worldPoints =
{
    {2, {-SIDE_LENGTH/2, -SIDE_LENGTH/2, 0}},
    {3, { SIDE_LENGTH/2, -SIDE_LENGTH/2, 0}},
    {4, { SIDE_LENGTH/2,  SIDE_LENGTH/2, 0}},
    {5, {-SIDE_LENGTH/2,  SIDE_LENGTH/2, 0}}
};

// ============================================================
// SHARED STRUCTS
// ============================================================

struct FrameData
{
    std::shared_ptr<cv::Mat> frame;
    int64_t timestamp;
};

struct Detection
{
    int id;
    std::vector<cv::Point2f> corners;
};

struct PoseWorld
{
    int id;
    double X;
    double Y;
    double yaw;
};

// ============================================================
// GLOBALS
// ============================================================

std::mutex mtxFrame;
std::condition_variable cvFrame;
std::shared_ptr<FrameData> sharedFrame;

std::atomic<bool> running(true);
std::atomic<bool> calibrated(false);

cv::Mat R_wc;
cv::Mat t_wc;

std::mutex mtxPose;
std::vector<PoseWorld> latestPoses;

// ============================================================
// HELPER
// ============================================================

void imshowScaled(const std::string& name, const cv::Mat& img)
{
    int maxW = 1600;
    int maxH = 900;

    double scale = std::min(
        maxW / (double)img.cols,
        maxH / (double)img.rows);

    if(scale < 1.0)
    {
        cv::Mat r;
        cv::resize(img, r, {}, scale, scale);
        cv::imshow(name, r);
    }
    else
        cv::imshow(name, img);
}

// ============================================================
// CAPTURE THREAD
// ============================================================

void captureThread(GstElement* sink)
{
    while(running)
    {
        GstSample* sample =
        gst_app_sink_try_pull_sample(
            GST_APP_SINK(sink),
            10000000);

        if(!sample) continue;

        GstBuffer* buffer =
        gst_sample_get_buffer(sample);

        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        GstCaps* caps =
        gst_sample_get_caps(sample);

        GstStructure* s =
        gst_caps_get_structure(caps,0);

        int w,h;
        gst_structure_get_int(s,"width",&w);
        gst_structure_get_int(s,"height",&h);

        auto frame =
        std::make_shared<cv::Mat>(
            h,w,CV_8UC3,map.data);

        auto clone =
        std::make_shared<cv::Mat>(
            frame->clone());

        gst_buffer_unmap(buffer,&map);
        gst_sample_unref(sample);

        auto fd =
        std::make_shared<FrameData>();

        fd->frame = clone;
        fd->timestamp =
        GST_BUFFER_PTS(buffer);

        {
            std::lock_guard<std::mutex> lock(mtxFrame);
            sharedFrame = fd;
        }

        cvFrame.notify_all();
    }
}

// ============================================================
// DETECTOR CREATE
// ============================================================

apriltag_detector_t* createDetector(int threads, float decimate)
{
    apriltag_family_t* tf =
        tag36h11_create();

    apriltag_detector_t* td =
        apriltag_detector_create();

    apriltag_detector_add_family(td,tf);

    td->nthreads = threads;
    td->quad_decimate = decimate;
    td->quad_sigma = 0.0;
    td->refine_edges = 1;

    return td;
}

// ============================================================
// DETECT TAGS
// ============================================================

std::vector<Detection>
detectTags(apriltag_detector_t* td,
           const cv::Mat& gray)
{
    image_u8_t img =
    {
        gray.cols,
        gray.rows,
        gray.cols,
        gray.data
    };

    zarray_t* detections =
    apriltag_detector_detect(td,&img);

    std::vector<Detection> result;

    for(int i=0;i<zarray_size(detections);i++)
    {
        apriltag_detection_t* det;
        zarray_get(detections,i,&det);

        Detection d;
        d.id = det->id;

        for(int k=0;k<4;k++)
        {
            d.corners.emplace_back(
                det->p[k][0],
                det->p[k][1]);
        }

        result.push_back(d);
    }

    apriltag_detections_destroy(detections);

    return result;
}

// ============================================================
// CALIBRATION
// ============================================================

bool calibrate(
    const std::vector<Detection>& dets,
    const cv::Mat& K,
    const cv::Mat& D)
{
    std::vector<cv::Point3f> obj;
    std::vector<cv::Point2f> img;

    for(auto& d : dets)
    {
        if(worldPoints.count(d.id)==0)
            continue;

        auto wp =
        worldPoints[d.id];

        obj.push_back(wp);

        cv::Point2f center =
        (d.corners[0]+
         d.corners[1]+
         d.corners[2]+
         d.corners[3]) * 0.25f;

        img.push_back(center);
    }

    if(obj.size()<4)
        return false;

    cv::Mat rvec,tvec;

    bool ok =
    cv::solvePnP(
        obj,img,
        K,D,
        rvec,tvec);

    if(!ok) return false;

    cv::Rodrigues(rvec,R_wc);
    t_wc = tvec;

    calibrated = true;

    std::cout << "\nCALIBRATION COMPLETE\n";

    return true;
}

// ============================================================
// CAMERA->WORLD
// ============================================================

cv::Point3f cameraToWorld(const cv::Mat& tvec)
{
    cv::Mat p =
    R_wc * tvec + t_wc;

    return
    cv::Point3f(
        p.at<double>(0),
        p.at<double>(1),
        p.at<double>(2));
}

// ============================================================
// TRACK THREAD
// ============================================================

void trackingThread(
    const cv::Mat& K,
    const cv::Mat& D)
{
    auto tdLow =
    createDetector(2,2.0);

    auto tdHigh =
    createDetector(2,1.0);

    while(running)
    {
        std::shared_ptr<FrameData> frame;

        {
            std::unique_lock<std::mutex> lock(mtxFrame);

            cvFrame.wait(lock,
                []{return sharedFrame!=nullptr;});

            frame = sharedFrame;
            sharedFrame.reset();
        }

        cv::Mat gray;
        cv::cvtColor(
            *frame->frame,
            gray,
            cv::COLOR_BGR2GRAY);

        cv::Mat low;
        cv::resize(
            gray,low,
            cv::Size(LOW_W,LOW_H));

        auto dets =
        detectTags(tdLow,low);

        if(!calibrated)
        {
            calibrate(dets,K,D);
            continue;
        }

        std::vector<PoseWorld> poses;

        for(auto& d : dets)
        {
            if(d.id!=TRACK_ID0 &&
               d.id!=TRACK_ID1)
               continue;

            std::vector<cv::Point2f> imgPts;

            float sx =
            gray.cols/(float)LOW_W;

            float sy =
            gray.rows/(float)LOW_H;

            for(auto&p:d.corners)
                imgPts.emplace_back(
                    p.x*sx,p.y*sy);

            std::vector<cv::Point3f> obj =
            {
                {-TAG_SIZE/2,-TAG_SIZE/2,0},
                { TAG_SIZE/2,-TAG_SIZE/2,0},
                { TAG_SIZE/2, TAG_SIZE/2,0},
                {-TAG_SIZE/2, TAG_SIZE/2,0}
            };

            cv::Mat rvec,tvec;

            cv::solvePnP(
                obj,imgPts,
                K,D,
                rvec,tvec);

            auto world =
            cameraToWorld(tvec);

            cv::Mat R;
            cv::Rodrigues(rvec,R);

            double yaw =
            atan2(
                R.at<double>(1,0),
                R.at<double>(0,0))
                *180/CV_PI;

            poses.push_back(
            {
                d.id,
                world.x,
                world.y,
                yaw
            });

            std::cout
            << "Tag "
            << d.id
            << " X="
            << world.x
            << " Y="
            << world.y
            << " yaw="
            << yaw
            << "\n";
        }

        {
            std::lock_guard<std::mutex>
            lock(mtxPose);

            latestPoses = poses;
        }
    }
}

// ============================================================
// VIS THREAD
// ============================================================

void visThread()
{
    while(running)
    {
        std::vector<PoseWorld> poses;

        {
            std::lock_guard<std::mutex>
            lock(mtxPose);

            poses = latestPoses;
        }

        if(!poses.empty())
        {
            cv::Mat img =
            cv::Mat::zeros(
                600,600,CV_8UC3);

            for(auto&p:poses)
            {
                int x =
                int(p.X*50+300);

                int y =
                int(p.Y*50+300);

                cv::circle(
                    img,
                    {x,y},
                    5,
                    {0,255,0},
                    -1);
            }

            imshowScaled(
                "World",img);
        }

        if(cv::waitKey(1)=='q')
            running=false;
    }
}

// ============================================================
// MAIN
// ============================================================

int main(int argc,char** argv)
{
    gst_init(&argc,&argv);

    std::string pipeline =
        "libcamerasrc "
        "exposure-time-mode=manual exposure-time=1500 " "analogue-gain-mode=manual analogue-gain=15 "
        "! video/x-raw,width=(4056/resoluion_divider),height=(3040/resoluion_divider),format=BGRx "
        "! videoconvert "
        "! video/x-raw,format=BGR "
        "! appsink name=sink drop=true max-buffers=1";

    GstElement* pipe =
    gst_parse_launch(
        pipeline.c_str(),
        nullptr);

    GstElement* sink =
    gst_bin_get_by_name(
        GST_BIN(pipe),
        "sink");

    gst_element_set_state(
        pipe,
        GST_STATE_PLAYING);

    cv::Mat K = (cv::Mat1d(3,3) <<
        (4009.22661/resoluion_divider),0,(2113.49677/resoluion_divider),
        0,(4020.48344/resoluion_divider),(1469.08894/resoluion_divider),
        0,0,1);

    cv::Mat D =
    (cv::Mat1d(1,5)<<
    -0.49,0.28,0,0,-0.09);

    std::thread cap(
        captureThread,
        sink);

    std::thread track(
        trackingThread,
        K,D);

    std::thread vis(
        visThread);

    cap.join();
    track.join();
    vis.join();

    gst_element_set_state(
        pipe,
        GST_STATE_NULL);

    gst_object_unref(pipe);

    return 0;
}
