/*
* Copyright (c) 2018 Intel Corporation.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// std includes
#include <iostream>
#include <stdio.h>
#include <thread>
#include <queue>
#include <mutex>
#include <syslog.h>

// OpenCV includes
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>

// MQTT
#include "mqtt.h"

using namespace std;
using namespace cv;
using namespace dnn;

// OpenCV-related variables
Mat frame, blob, poseBlob;
VideoCapture cap;
int delay = 5;
Net net, posenet;
bool poseChecked = false;
// application parameters
String model;
String config;
String posemodel;
String poseconfig;
int backendId;
int targetId;
int rate;

// ShoppingInfo contains statistics for the shopping information tracked by this application.
struct ShoppingInfo
{
    int shoppers;
    int lookers;
    std::vector<Rect> faces;
    std::map<int, bool> lookMap;
};

// currentInfo contains the latest ShoppingInfo tracked by the application.
ShoppingInfo currentInfo;


std::queue<Mat> nextImage;
String currentPerf;

std::mutex m, m1, m2;

const char* keys =
    "{ help  h     | | Print help message. }"
    "{ device d    | 0 | camera device number. }"
    "{ input i     | | Path to input image or video file. Skip this argument to capture frames from a camera.}"
    "{ model m     | | Path to .bin file of model containing face recognizer. }"
    "{ config c    | | Path to .xml file of model containing network configuration. }"
    "{ posemodel pm     | | Path to .bin file of face pose model. }"
    "{ poseconfig pc    | | Path to a .xml file of face pose model containing network configuration. }"
    "{ backend b    | 0 | Choose one of computation backends: "
                        "0: automatically (by default), "
                        "1: Halide language (http://halide-lang.org/), "
                        "2: Intel's Deep Learning Inference Engine (https://software.intel.com/openvino-toolkit), "
                        "3: OpenCV implementation }"
    "{ target t     | 0 | Choose one of target computation devices: "
                        "0: CPU target (by default), "
                        "1: OpenCL, "
                        "2: OpenCL fp16 (half-float precision), "
                        "3: VPU }"
    "{ rate r      | 1 | number of seconds between data updates to MQTT server. }";                        


// nextImageAvailable returns the next image from the queue in a thread-safe way
Mat nextImageAvailable() {
    Mat rtn;
    m.lock();
    if (!nextImage.empty()) {
        rtn = nextImage.front();
        nextImage.pop();
    }
    m.unlock();
    return rtn;
}

// addImage adds an image to the queue in a thread-safe way
void addImage(Mat img) {
    m.lock();
    if (nextImage.empty()) {
        nextImage.push(img);
    }
    m.unlock();
}

// getCurrentInfo returns the most-recent ShoppingInfo for the application.
ShoppingInfo getCurrentInfo() {
    ShoppingInfo rtn;
    m2.lock();
    rtn = currentInfo;
    m2.unlock();
    return rtn;
}

// updateInfo uppdates the current ShoppingInfo for the application to the highest values
// during the current time period.
void updateInfo(ShoppingInfo info) {
    m2.lock();
    if (currentInfo.shoppers != info.shoppers) {
        currentInfo.shoppers = info.shoppers;
        currentInfo.faces = info.faces;
    }

    if (currentInfo.lookers != info.lookers) {
        currentInfo.lookers = info.lookers;
        currentInfo.lookMap = info.lookMap;
    }
    m2.unlock();
}

// resetInfo resets the current ShoppingInfo for the application.
void resetInfo() {
    m2.lock();
    currentInfo.shoppers = 0;
    currentInfo.lookers = 0;
    currentInfo.faces.clear();
    currentInfo.lookMap.clear();
    m2.unlock();
}

// getCurrentPerf returns a display string with the most current performance stats for the Inference Engine.
string getCurrentPerf() {
    string rtn;
    m1.lock();
    rtn = currentPerf;
    m1.unlock();
    return rtn;
}

// savePerformanceInfo sets the display string with the most current performance stats for the Inference Engine.
void savePerformanceInfo() {
    m1.lock();

    std::vector<double> faceTimes, poseTimes;
    double freq = getTickFrequency() / 1000;
    double t = net.getPerfProfile(faceTimes) / freq;
    double t2;
    if (poseChecked) {
        t2 = posenet.getPerfProfile(poseTimes) / freq;
    }
    std::string label = format("Face inference time: %.2f ms, Pose inference time: %.2f ms", t, t2);

    currentPerf = label;
    m1.unlock();
}

// publish MQTT message with a JSON payload
void publishMQTTMessage(const string& topic, const ShoppingInfo& info)
{
    std::ostringstream s;
    s << "{\"shoppers\": \"" << info.shoppers << "\",";
    s << "\"lookers\": \"" << info.lookers << "\"}";
    std::string payload = s.str();

    mqtt_publish(topic, payload);

    string msg = "MQTT message published to topic: " + topic;
    syslog(LOG_INFO, "%s", msg.c_str());
    syslog(LOG_INFO, "%s", payload.c_str());
}

// message handler for the MQTT subscription for the any desired control channel topic
int handleMQTTControlMessages(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    string topic = topicName;
    string msg = "MQTT message received: " + topic;
    syslog(LOG_INFO, "%s", msg.c_str());

    return 1;
}

// Function called by worker thread to process the next available video frame.
void frameRunner() {
    for (;;) {
        Mat next = nextImageAvailable();
        if (!next.empty()) {
            // convert to 4d vector as required by model, and set as input
            blobFromImage(next, blob, 1.0, Size(672, 384));
            net.setInput(blob);
            Mat prob = net.forward();

            // get faces
            std::vector<float> confidences;
            std::vector<Rect> faces;
            std::map<int, bool> tempMap;
            int looking = 0;
            float* data = (float*)prob.data;
            for (size_t i = 0; i < prob.total(); i += 7)
            {
                float confidence = data[i + 2];
                if (confidence >= 0.35)
                {
                    int left = (int)(data[i + 3] * frame.cols);
                    int top = (int)(data[i + 4] * frame.rows);
                    int right = (int)(data[i + 5] * frame.cols);
                    int bottom = (int)(data[i + 6] * frame.rows);
                    int width = right - left + 1;
                    int height = bottom - top + 1;

                    faces.push_back(Rect(left, top, width, height));
                    confidences.push_back(confidence);
                }
            }            

            // look for poses
            for(auto const& r: faces) {
                // make sure the face rect is completely inside the main Mat
                if ((r & Rect(0, 0, next.cols, next.rows)) != r) {
                    continue;
                }

                std::vector<Mat> outs;
                std::vector<String> names{"angle_y_fc", "angle_p_fc", "angle_r_fc"};
                cv::Mat face = next(r);
                // convert to 4d vector, and process thru neural network
                blobFromImage(face, poseBlob, 1.0, Size(60, 60));
                posenet.setInput(poseBlob);
                posenet.forward(outs, names);
                poseChecked = true;
                int key = r.x+r.y+r.width+r.height;
                tempMap[key] = false;
                // the shopper is looking if their head is tilted within a 40 degree angle relative to the shelf
                if ( (outs[0].at<float>(0) > -25) && (outs[0].at<float>(0) < 25) &&
                     (outs[1].at<float>(0) > -25) && (outs[1].at<float>(0) < 25)) {
                    tempMap[key] = true;
                    looking++;                  
                }
                
            }
           
            // retail data
            ShoppingInfo info;
            info.shoppers = faces.size();
            info.lookers = looking;
            info.faces = faces;
            info.lookMap = tempMap;
            updateInfo(info);

            savePerformanceInfo();
        }
    }
}

// Function called by worker thread to handle MQTT updates. Pauses for rate second(s) between updates.
void messageRunner() {
    for (;;) {
        publishMQTTMessage("retail/traffic", getCurrentInfo());
        resetInfo();
        std::this_thread::sleep_for(std::chrono::seconds(rate));
    }
}

int main(int argc, char** argv)
{
    // parse command parameters
    CommandLineParser parser(argc, argv, keys);
    parser.about("Use this script to using OpenVINO.");
    if (argc == 1 || parser.has("help"))
    {
        parser.printMessage();
        return 0;
    }

    model = parser.get<String>("model");
    config = parser.get<String>("config");
    backendId = parser.get<int>("backend");
    targetId = parser.get<int>("target");
    rate = parser.get<int>("rate");

    posemodel = parser.get<String>("posemodel");
    poseconfig = parser.get<String>("poseconfig");

    // connect MQTT messaging
    int result = mqtt_start(handleMQTTControlMessages);
    if (result == 0) {
        syslog(LOG_INFO, "MQTT started.");
    } else {
        syslog(LOG_INFO, "MQTT NOT started: have you set the ENV varables?");
    }

    mqtt_connect();

    // open face model
    net = readNet(model, config);
    net.setPreferableBackend(backendId);
    net.setPreferableTarget(targetId);

    // open pose model
    posenet = readNet(posemodel, poseconfig);
    posenet.setPreferableBackend(backendId);
    posenet.setPreferableTarget(targetId);

    // open video capture source
    if (parser.has("input")) {
        cap.open(parser.get<String>("input"));

        // also adjust delay so video playback matches the number of FPS in the file
        double fps = cap.get(CAP_PROP_FPS);
        delay = 1000/fps;
    }
    else
        cap.open(parser.get<int>("device"));


    if (!cap.isOpened()) {
        cerr << "ERROR! Unable to open video source\n";
        return -1;
    }

    // cap.set(cv::CV_CAP_PROP_FRAME_WIDTH,1600);
    // cap.set(cv::CV_CAP_PROP_FRAME_HEIGHT,900);

    // start worker threads
    std::thread t1(frameRunner);
    std::thread t2(messageRunner);

    // read video input data
    for (;;) {
        cap.read(frame);

        if (frame.empty()) {
            cerr << "ERROR! blank frame grabbed\n";
            break;
        }

        addImage(frame);

        string label = getCurrentPerf();
        putText(frame, label, Point(0, 15), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 0));

        ShoppingInfo info = getCurrentInfo();
        label = format("Shoppers: %d, lookers: %d", info.shoppers, info.lookers);
        putText(frame, label, Point(0, 40), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 0));

        //If looking, bound face by white box, else red box
        for(auto const& r: info.faces) {
            int key = r.x+r.y+r.width+r.height;
            if(info.lookMap[key] == true)
                rectangle(frame, r, Scalar(255,255,255), 2);
            else
                rectangle(frame, r, Scalar(0,0,229), 2);
        }

        imshow("Shopper Gaze Monitor", frame);

        if (waitKey(delay) >= 0) {
            // TODO: signal threads to exit
            break;
        }
    }

    // TODO: wait for worker threads to exit
    //t1.join();

    // disconnect MQTT messaging
    mqtt_disconnect();
    mqtt_close();

    return 0;
}
