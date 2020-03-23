/*
 * ImageInput.cpp
 *
 */

#include <ctime>
#include <string>
#include <list>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <log4cpp/Category.hh>
#include <log4cpp/Priority.hh>

#include "ImageInput.h"

ImageInput::~ImageInput() {
}

cv::Mat & ImageInput::getImage() {
    return _img;
}

time_t ImageInput::getTime() {
    return _time;
}

void ImageInput::setOutputDir(const std::string & outDir) {
    _outDir = outDir;
}

void ImageInput::saveImage() {
    if (_outDir.length() == 0) {
        log4cpp::Category::getRoot() << log4cpp::Priority::ERROR << "Try save image empty path";
        return;
    }
    struct tm date;
    localtime_r(&_time, &date);
    char filename[PATH_MAX];
    strftime(filename, PATH_MAX, "/%Y%m%d-%H%M%S.png", &date);
    std::string path = _outDir + filename;
    if (cv::imwrite(path, _img)) {
        log4cpp::Category::getRoot() << log4cpp::Priority::INFO << "Image saved to " + path;
    }
}

DirectoryInput::DirectoryInput(const Directory & directory) :
    _directory(directory) {
    _filenameList = _directory.list();
    _filenameList.sort();
    _itFilename = _filenameList.begin();
}

bool DirectoryInput::nextImage(std::string & path) {
    log4cpp::Category & rlog = log4cpp::Category::getRoot();
    if (_itFilename == _filenameList.end()) {
        return false;
    }
    path = _directory.fullpath(*_itFilename);

    _img = cv::imread(path.c_str());

    // read time from file name

    struct tm date;
    memset(&date, 0, sizeof(date));
    date.tm_year = atoi(_itFilename->substr(0, 4).c_str()) - 1900;
    date.tm_mon = atoi(_itFilename->substr(4, 2).c_str()) - 1;
    date.tm_mday = atoi(_itFilename->substr(6, 2).c_str());
    date.tm_hour = atoi(_itFilename->substr(9, 2).c_str());
    date.tm_min = atoi(_itFilename->substr(11, 2).c_str());
    date.tm_sec = atoi(_itFilename->substr(13, 2).c_str());
    _time = mktime(&date);

    rlog << log4cpp::Priority::INFO << "Processing " << *_itFilename << " of " << ctime(&_time);

    // save copy of image if requested
    if (!_outDir.empty()) {
        saveImage();
    }

    _itFilename++;
    return true;
}

CameraInput::CameraInput(int device) {
    _capture.open(device);
}

bool CameraInput::nextImage(std::string & path) {
    time(&_time);
    // read image from camera
    bool success = _capture.read(_img);

    log4cpp::Category::getRoot() << log4cpp::Priority::INFO << "Image captured: " << success;

    // save copy of image if requested
    if (success && !_outDir.empty()) {
        saveImage();
    }

    return success;
}


InotifyInput::InotifyInput(const std::string path, int timeout):
    _path(path),
    _timeout(timeout) {

    log4cpp::Category & rlog = log4cpp::Category::getRoot();
    _inotifyFd = inotify_init();

    if (_inotifyFd == -1) {
        rlog << log4cpp::Priority::ERROR << ": inotify_init error:" << std::strerror(errno);
    } else {
        _inotifyWatch = inotify_add_watch(_inotifyFd, _path.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO );
        if ( _inotifyWatch == -1) {
            rlog << log4cpp::Priority::ERROR << ": inotify_add_watch: " << _path << " :" << std::strerror(errno);
        }
    }
}

InotifyInput::~InotifyInput(void) {

    if (_inotifyWatch != -1) {
        inotify_rm_watch(_inotifyFd, _inotifyWatch);
    }

    if (_inotifyFd != -1) {
        close(_inotifyFd);
    }

}

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

bool InotifyInput::nextImage(std::string & path) {
    char buf[BUF_LEN] __attribute__ ((aligned(8)));
    struct pollfd fds[1];
    fds[0].fd = _inotifyFd;
    fds[0].events = POLLIN;

    log4cpp::Category & rlog = log4cpp::Category::getRoot();

    while (_files.empty()) {
        int poll_ret = poll(fds, 1,  _timeout);

        if (poll_ret == 0) { // timeout
            continue;
        }

        if (poll_ret < 0) { //error
            rlog << log4cpp::Priority::ERROR << ": poll error:" << std::strerror(errno);
            return false;
        }
        bzero(buf, sizeof(buf));
        int numRead = read(_inotifyFd, buf, BUF_LEN);

        if (numRead == 0) {
            return false;
        }

        if (numRead == -1) {
            rlog << log4cpp::Priority::ERROR << ": read error:" << std::strerror(errno);
            return false;
        }

        for (char * p = buf; p < buf + numRead; ) {
            struct inotify_event * event = (struct inotify_event *) p;
            if (Directory::hasExtension(event->name, ".png")) {
                /*                printf("%s %s %s %x\n", event->name,
                                       event->mask & IN_CLOSE_WRITE? "IN_CLOSE_WRITE":"",
                                       event->mask & IN_MOVED_TO? "IN_MOVED_TO":"",
                                       event->mask);*/
                _files.push_back(std::string(event->name));
            }
            p += sizeof(struct inotify_event) + event->len;
        }
        if (!_files.empty()) {
            _files.sort();
            break;
        }
    }

    if (_files.empty()) {
        return false;
    }

    path = _path + "/" + _files.front();
    auto _itFilename = _files.front();
    _files.pop_front();

    _img = cv::imread(path.c_str());

    struct tm date;
    memset(&date, 0, sizeof(date));
    date.tm_year = atoi(_itFilename.substr(0, 4).c_str()) - 1900;
    date.tm_mon = atoi(_itFilename.substr(4, 2).c_str()) - 1;
    date.tm_mday = atoi(_itFilename.substr(6, 2).c_str());
    date.tm_hour = atoi(_itFilename.substr(9, 2).c_str());
    date.tm_min = atoi(_itFilename.substr(11, 2).c_str());
    date.tm_sec = atoi(_itFilename.substr(13, 2).c_str());
    _time = mktime(&date);

    rlog << log4cpp::Priority::INFO << log4cpp::Priority::INFO << "Processing " << path << " of " << ctime(&_time);

    return true;
}
