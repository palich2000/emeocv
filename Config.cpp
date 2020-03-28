/*
 * Config.cpp
 *
 */

#include <opencv2/highgui/highgui.hpp>
#include <iostream>
#include "Config.h"

Config::Config() :
    _rotationDegrees(0),
    _ocrMaxDist(5e5),
    _digitMinHeight(20),
    _digitMaxHeight(90),
    _digitYAlignment(10),
    _cannyThreshold1(100),
    _cannyThreshold2(200),
    _trainingDataFilename("trainctr.yml") {
}

void Config::saveConfig(const std::string & configPath) {
    _configPath = configPath;
    saveConfig();
}

void Config::saveConfig() {
    std::cout << "Save config to " << _configPath << "\n";
    cv::FileStorage fs(_configPath, cv::FileStorage::WRITE);
    fs << "rotationDegrees" << _rotationDegrees;
    fs << "cannyThreshold1" << _cannyThreshold1;
    fs << "cannyThreshold2" << _cannyThreshold2;
    fs << "digitMinHeight" << _digitMinHeight;
    fs << "digitMaxHeight" << _digitMaxHeight;
    fs << "digitYAlignment" << _digitYAlignment;
    fs << "ocrMaxDist" << _ocrMaxDist;
    fs << "trainingDataFilename" << _trainingDataFilename;
    fs.release();
}

void Config::loadConfig(const std::string & configPath) {
    _configPath = configPath;
    loadConfig();
}

void Config::loadConfig() {
    std::cout << "Load config from " << _configPath << "\n";
    cv::FileStorage fs(_configPath, cv::FileStorage::READ);
    if (fs.isOpened()) {
        fs["rotationDegrees"] >> _rotationDegrees;
        fs["cannyThreshold1"] >> _cannyThreshold1;
        fs["cannyThreshold2"] >> _cannyThreshold2;
        fs["digitMinHeight"] >> _digitMinHeight;
        fs["digitMaxHeight"] >> _digitMaxHeight;
        fs["digitYAlignment"] >> _digitYAlignment;
        fs["ocrMaxDist"] >> _ocrMaxDist;
        fs["trainingDataFilename"] >> _trainingDataFilename;
        fs.release();
    } else {
        // no config file - create an initial one with default values
        saveConfig();
    }
}
