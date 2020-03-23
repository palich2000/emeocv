/*
 * ImageInput.h
 *
 */

#ifndef IMAGEINPUT_H_
#define IMAGEINPUT_H_

#include <ctime>
#include <string>
#include <list>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "Directory.h"

class ImageInput {
public:
    virtual ~ImageInput();

    virtual bool nextImage(std::string & path) = 0;

    virtual cv::Mat & getImage();
    virtual time_t getTime();
    virtual void setOutputDir(const std::string & outDir);
    virtual void saveImage();

protected:
    cv::Mat _img;
    time_t _time;
    std::string _outDir = "";
};

class DirectoryInput: public ImageInput {
public:
    DirectoryInput(const Directory & directory);

    virtual bool nextImage(std::string & path);

private:
    Directory _directory;
    std::list<std::string>::const_iterator _itFilename;
    std::list<std::string> _filenameList;
};

class CameraInput: public ImageInput {
public:
    CameraInput(int device);

    virtual bool nextImage(std::string & path);

private:
    cv::VideoCapture _capture;
};

class InotifyInput: public ImageInput {
public:
    InotifyInput(const std::string path, int timeout);
    ~InotifyInput();
    virtual bool nextImage(std::string & path);

private:
    std::string _path;
    int _inotifyFd;
    int _inotifyWatch;
    int _timeout;
    std::list<std::string> _files;
};

#endif /* IMAGEINPUT_H_ */
