#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <vector>
#include <chrono>

#include <opencv2/opencv.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

// ============================================================
// USER PARAMETERS
// ============================================================

// Divider for processing resolution (kept for future full-res ROI logic)
constexpr int resolution_divider = 1;

// Visualization size (half HQ resolution)
constexpr int DISPLAY_W = 2028;
constexpr int DISPLAY_H = 1520;

// AprilTag physical size
constexpr double TAG_SIZE = 0.10;

// Size of calibration square (meters)
constexpr double CALIB_SQUARE = 0.8;

// Tracking tag IDs
constexpr int TRACK_TAG0 = 0;
constexpr int TRACK_TAG1 = 1;

// ============================================================
// GLOBAL STATE
// ============================================================

std::atomic<bool> running(true);
std::atomic<bool> calibrated(false);

std::mutex frameMutex;
cv::Mat latestFrame;

// Frame counter for JSON output
std::atomic<uint64_t> frameCounter(0);

// ============================================================
// POSE STRUCT
// ============================================================

struct Pose
{
    double x = 0;
    double y = 0;
    double yaw = 0;
};

// Last known poses
std::mutex poseMutex;
Pose tag0Pose;
Pose tag1Pose;

// Camera -> world transform
cv::Mat R_wc;
cv::Mat t_wc;

// ============================================================
// CAMERA INTRINSICS
// ============================================================

cv::Mat K = (cv::Mat1d(3,3) <<
4009.22661,0,2113.49677,
0,4020.48344,1469.08894,
0,0,1);

cv::Mat D = (cv::Mat1d(1,5) <<
-0.49,0.28,0,0,-0.09);

// ============================================================
// WORLD COORDINATES OF CALIBRATION TAGS
// ============================================================

std::map<int,std::vector<cv::Point3f>> worldTagCorners =
{
{2,{
{-CALIB_SQUARE/2,-CALIB_SQUARE/2,0},
{-CALIB_SQUARE/2+TAG_SIZE,-CALIB_SQUARE/2,0},
{-CALIB_SQUARE/2+TAG_SIZE,-CALIB_SQUARE/2+TAG_SIZE,0},
{-CALIB_SQUARE/2,-CALIB_SQUARE/2+TAG_SIZE,0}
}},
{3,{
{CALIB_SQUARE/2,-CALIB_SQUARE/2,0},
{CALIB_SQUARE/2+TAG_SIZE,-CALIB_SQUARE/2,0},
{CALIB_SQUARE/2+TAG_SIZE,-CALIB_SQUARE/2+TAG_SIZE,0},
{CALIB_SQUARE/2,-CALIB_SQUARE/2+TAG_SIZE,0}
}},
{4,{
{CALIB_SQUARE/2,CALIB_SQUARE/2,0},
{CALIB_SQUARE/2+TAG_SIZE,CALIB_SQUARE/2,0},
{CALIB_SQUARE/2+TAG_SIZE,CALIB_SQUARE/2+TAG_SIZE,0},
{CALIB_SQUARE/2,CALIB_SQUARE/2+TAG_SIZE,0}
}},
{5,{
{-CALIB_SQUARE/2,CALIB_SQUARE/2,0},
{-CALIB_SQUARE/2+TAG_SIZE,CALIB_SQUARE/2,0},
{-CALIB_SQUARE/2+TAG_SIZE,CALIB_SQUARE/2+TAG_SIZE,0},
{-CALIB_SQUARE/2,CALIB_SQUARE/2+TAG_SIZE,0}
}}
};

// ============================================================
// CREATE APRILTAG DETECTOR
// ============================================================

apriltag_detector_t* createDetector()
{
    auto tf = tag36h11_create();
    auto td = apriltag_detector_create();

    apriltag_detector_add_family(td,tf);

    td->quad_decimate = 2.0;
    td->nthreads = 4;
    td->refine_edges = 1;

    return td;
}

// ============================================================
// CAMERA CAPTURE THREAD
// ============================================================

void captureThread(GstElement* sink)
{
    while(running)
    {
        auto sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

        auto buffer = gst_sample_get_buffer(sample);

        GstMapInfo map;
        gst_buffer_map(buffer,&map,GST_MAP_READ);

        auto caps = gst_sample_get_caps(sample);
        auto s = gst_caps_get_structure(caps,0);

        int w,h;
        gst_structure_get_int(s,"width",&w);
        gst_structure_get_int(s,"height",&h);

        cv::Mat frame(h,w,CV_8UC3,map.data);

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = frame.clone();
        }

        gst_buffer_unmap(buffer,&map);
        gst_sample_unref(sample);
    }
}

// ============================================================
// CAMERA -> WORLD TRANSFORMATION
// ============================================================

cv::Point3f camToWorld(cv::Mat tvec)
{
    if(R_wc.empty() || t_wc.empty())
        return {0,0,0};

    cv::Mat p = R_wc * tvec + t_wc;

    return {
        p.at<double>(0),
        p.at<double>(1),
        p.at<double>(2)
    };
}

// ============================================================
// CALIBRATION USING MULTIPLE TAG CORNERS
// ============================================================

bool calibrate(
    std::vector<cv::Point2f>& imgPts,
    std::vector<cv::Point3f>& objPts)
{
    if(imgPts.size() < 8)
        return false;

    cv::Mat rvec;

    cv::solvePnP(objPts,imgPts,K,D,rvec,t_wc);

    cv::Rodrigues(rvec,R_wc);

    calibrated = true;

    std::cout << "Calibration successful\n";

    return true;
}

// ============================================================
// TRACKING THREAD
// ============================================================

void trackingThread()
{
    auto detector = createDetector();

    while(running)
    {

        cv::Mat frame;

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if(latestFrame.empty()) continue;
            frame = latestFrame.clone();
        }

        frameCounter++;

        cv::Mat gray;
        cv::cvtColor(frame,gray,cv::COLOR_BGR2GRAY);

        image_u8_t img =
        {
            gray.cols,
            gray.rows,
            gray.cols,
            gray.data
        };

        auto detections =
        apriltag_detector_detect(detector,&img);

        std::vector<cv::Point2f> calibImg;
        std::vector<cv::Point3f> calibObj;

        for(int i=0;i<zarray_size(detections);i++)
        {

            apriltag_detection_t* det;
            zarray_get(detections,i,&det);

            // collect calibration tag corners
            if(worldTagCorners.count(det->id))
            {
                for(int k=0;k<4;k++)
                {
                    calibImg.emplace_back(det->p[k][0],det->p[k][1]);
                    calibObj.push_back(worldTagCorners[det->id][k]);
                }
            }

            // tracking tags
            if(calibrated &&
               (det->id==TRACK_TAG0 || det->id==TRACK_TAG1))
            {

                std::vector<cv::Point3f> obj =
                {
                {-TAG_SIZE/2,-TAG_SIZE/2,0},
                { TAG_SIZE/2,-TAG_SIZE/2,0},
                { TAG_SIZE/2, TAG_SIZE/2,0},
                {-TAG_SIZE/2, TAG_SIZE/2,0}
                };

                std::vector<cv::Point2f> imgPts;

                for(int k=0;k<4;k++)
                    imgPts.emplace_back(det->p[k][0],det->p[k][1]);

                cv::Mat rvec,tvec;

                cv::solvePnP(obj,imgPts,K,D,rvec,tvec);

                auto world = camToWorld(tvec);

                cv::Mat R;
                cv::Rodrigues(rvec,R);

                double yaw =
                atan2(R.at<double>(1,0),
                      R.at<double>(0,0)) * 180 / CV_PI;

                std::lock_guard<std::mutex> lock(poseMutex);

                if(det->id==0)
                    tag0Pose = {world.x,world.y,yaw};
                else
                    tag1Pose = {world.x,world.y,yaw};

            }

        }

        if(!calibrated)
            calibrate(calibImg,calibObj);

        apriltag_detections_destroy(detections);

        // =====================================================
        // JSON OUTPUT
        // =====================================================

        auto ts =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

        Pose p0,p1;

        {
            std::lock_guard<std::mutex> lock(poseMutex);
            p0 = tag0Pose;
            p1 = tag1Pose;
        }

        std::cout
        << "{\"ts\":" << ts
        << ",\"frame\":" << frameCounter
        << ",\"tag0\":{\"x\":" << p0.x
        << ",\"y\":" << p0.y
        << ",\"yaw\":" << p0.yaw
        << "},\"tag1\":{\"x\":" << p1.x
        << ",\"y\":" << p1.y
        << ",\"yaw\":" << p1.yaw
        << "}}"
        << std::endl;
    }
}

// ============================================================
// VISUALIZATION THREAD
// ============================================================

void visThread()
{
    cv::namedWindow("Tracking",cv::WINDOW_NORMAL);
    cv::resizeWindow("Tracking",DISPLAY_W,DISPLAY_H);

    while(running)
    {

        cv::Mat frame;

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if(latestFrame.empty()) continue;
            frame = latestFrame.clone();
        }

        cv::resize(frame,frame,
        cv::Size(DISPLAY_W,DISPLAY_H));

        Pose p0,p1;

        {
            std::lock_guard<std::mutex> lock(poseMutex);
            p0 = tag0Pose;
            p1 = tag1Pose;
        }

        cv::putText(frame,
        "Tag0: "+std::to_string(p0.x)+" "+std::to_string(p0.y),
        {40,40},
        cv::FONT_HERSHEY_SIMPLEX,
        1.2,
        {0,255,0},
        2);

        cv::putText(frame,
        "Tag1: "+std::to_string(p1.x)+" "+std::to_string(p1.y),
        {40,80},
        cv::FONT_HERSHEY_SIMPLEX,
        1.2,
        {0,0,255},
        2);

        cv::imshow("Tracking",frame);

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
    "libcamerasrc ! "
    "video/x-raw,width=4056,height=3040,format=BGRx ! "
    "videoconvert ! "
    "video/x-raw,format=BGR ! "
    "appsink name=sink";

    auto pipe =
    gst_parse_launch(pipeline.c_str(),nullptr);

    auto sink =
    gst_bin_get_by_name(GST_BIN(pipe),"sink");

    gst_element_set_state(pipe,GST_STATE_PLAYING);

    std::thread cap(captureThread,sink);
    std::thread track(trackingThread);
    std::thread vis(visThread);

    cap.join();
    track.join();
    vis.join();

}
