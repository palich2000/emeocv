/*
 * main.cpp
 *
 */

#include <string>
#include <list>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mosquittopp.h>
#include <thread>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <log4cpp/Category.hh>
#include <log4cpp/Appender.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/Layout.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/SimpleLayout.hh>
#include <log4cpp/Priority.hh>

#include "Config.h"
#include "Directory.h"
#include "ImageProcessor.h"
#include "KNearestOcr.h"
#include "Plausi.h"
#include "RRDatabase.h"

static int delay = 1000;

#ifndef VERSION
#define VERSION "0.9.7"
#endif
volatile bool do_exit = false;

template<typename ... Args>
std::string string_format( const std::string & format, Args ... args ) {
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1;
    if( size <= 0 ) {
        throw std::runtime_error( "Error during formatting." );
    }
    std::unique_ptr<char[]> buf( new char[ size ] );
    snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 );
}

#define ONLINE "Online"
#define OFFLINE "Offline"

const char * mqtt_host = "192.168.0.106";
const int mqtt_port = 8883;
const int mqtt_keepalive = 60;
#define TOPIC_LWT "tele/%s/LWT"
#define TOPIC_SENSOR "tele/%s/SENSOR"

class mosquittoPP : public mosqpp::mosquittopp {
public:
    //using  mosqpp::mosquittopp::mosquittopp;
    mosquittoPP(const char * id = NULL, bool clean_session = true, const char * hostname = "unknown");
    void publish_lwt(bool online);
    void publish_state(double gas_value);
    void on_connect(int rc);
    std::string make_topic(const std::string & tmpl);
private:
    std::string _hostname = "unknown";
};

mosquittoPP::mosquittoPP(const char * id, bool clean_session, const char * hostname):
    mosqpp::mosquittopp(id, clean_session), _hostname(hostname) {
}

std::string mosquittoPP::make_topic(const std::string & tmpl) {
    return string_format(tmpl, _hostname.c_str());
}

void mosquittoPP::publish_lwt(bool online) {
    const char * msg = online ? ONLINE : OFFLINE;
    publish(NULL, make_topic(TOPIC_LWT).c_str(), strlen(msg), msg, 0, true);
}

void mosquittoPP::publish_state(double gas_value) {

    std::stringstream ss;
    char tm_buffer[32] = {};
    time_t now = time(NULL);
    struct tm * tm_info = localtime(&now);
    strftime(tm_buffer, 26, "%Y-%m-%dT%H:%M:%S", tm_info);

    ss << "{\"Time\":\"" << tm_buffer << "\"," << "\"GAS\":" << std::fixed << std::setprecision(3) << gas_value << "}";
    std::string msg = ss.str();
    std::cout << ss.str() << std::endl;
    publish(NULL, make_topic(TOPIC_SENSOR).c_str(), msg.length(), msg.c_str(), 0, false);
}

void mosquittoPP::on_connect(int rc) {
    log4cpp::Category & rlog = log4cpp::Category::getRoot();
    switch (rc) {
    case 0:
        rlog << log4cpp::Priority::INFO << "Connected to mqtt server.";
        subscribe(NULL, "stat/+/POWER", 0);
        publish_lwt(true);
        break;
    case 1:
        rlog << log4cpp::Priority::ERROR << "Connection refused (unacceptable protocol version).";
        break;
    case 2:
        rlog << log4cpp::Priority::ERROR << "Connection refused (identifier rejected).";
        break;
    case 3:
        rlog << log4cpp::Priority::ERROR << "Connection refused (broker unavailable).";
        break;
    default:
        rlog << log4cpp::Priority::ERROR << "Unknown connection error. (%s)" << mosqpp::strerror(rc);
        break;
    }
    if (rc != 0) {
        sleep(10);
    }
}

static
void mosq_thread_loop(mosquittoPP * mosq) {
    log4cpp::Category & rlog = log4cpp::Category::getRoot();

    while (!do_exit) {
        int res = mosq->loop(1000, 1);
        switch (res) {
        case MOSQ_ERR_SUCCESS:
            break;
        case MOSQ_ERR_NO_CONN: {
            int res = mosq->connect(mqtt_host, mqtt_port, mqtt_keepalive);
            if (res) {
                rlog << log4cpp::Priority::ERROR << "Can't connect to Mosquitto server %s" << mosqpp::strerror(res);
                sleep(30);
            }
            break;
        }
        case MOSQ_ERR_INVAL:
        case MOSQ_ERR_NOMEM:
        case MOSQ_ERR_CONN_LOST:
        case MOSQ_ERR_PROTOCOL:
        case MOSQ_ERR_ERRNO:
            rlog << log4cpp::Priority::ERROR <<  strerror(errno) << " " << mosqpp::strerror(res);
            mosq->disconnect();
            rlog << log4cpp::Priority::ERROR << "disconnected";
            sleep(10);
            rlog << log4cpp::Priority::ERROR << "Try to reconnect";
            int res = mosq->connect(mqtt_host, mqtt_port, mqtt_keepalive);
            if (res) {
                rlog << log4cpp::Priority::ERROR << "Can't connect to Mosquitto server " << mosqpp::strerror(res);
            } else {
                rlog << log4cpp::Priority::ERROR << "Connected";
            }
            break;
        }
    }
}

static void testOcr(ImageInput * pImageInput) {
    log4cpp::Category::getRoot().info("testOcr");

    Config config;
    config.loadConfig();
    ImageProcessor proc(config);
    proc.debugWindow();
    proc.debugDigits();

    Plausi plausi(50, 3);

    KNearestOcr ocr(config);
    if (! ocr.loadTrainingData()) {
        std::cout << "Failed to load OCR training data\n";
        return;
    }
    std::cout << "OCR training data loaded.\n";
    std::cout << "<q> to quit.\n";
    std::string path;
    int key = 0;
    while (pImageInput->nextImage(path)) {
        proc.setInput(pImageInput->getImage());
        proc.process();

        std::string result = ocr.recognize(proc.getOutput());
        time_t time = pImageInput->getTime();
        char * str_time = std::ctime(&time);
        str_time[strlen(str_time) - 1] = 0;
        std::cout << str_time << "  ";
        std::cout << std::left << std::setw(8) << result;
        if (result.find("?") != std::string::npos) {
            std::cout << "Learn" << path << "  " << std::endl;
            for(std::string::size_type i = 0; i < result.size(); ++i) {
                if (result[i] == '?') {
                    key = ocr.learn(proc.getOutput()[i]);
                }
            }
            if (key == 'q' || key == 's') {
                std::cout << "Quit\n";
                break;
            }
        }
        if (plausi.check(result, pImageInput->getTime())) {
            std::cout << "  " << std::fixed << std::setprecision(3) << plausi.getCheckedValue() << std::endl;
        } else {
            std::cout << "  -------!" << std::endl;
        }
        key = cv::waitKey(delay) & 255;

        if (key == 'q') {
            std::cout << "Quit\n";
            break;
        }
    }
    if (key != 'q' && ocr.hasTrainingData()) {
        std::cout << "Saving training data\n";
        ocr.saveTrainingData();
    }
}

static void mqttOcr(ImageInput * pImageInput, mosquittoPP * mosq) {
    log4cpp::Category::getRoot().info("mqttOcr");

    Config config;
    config.loadConfig();
    ImageProcessor proc(config);

    Plausi plausi(50, 3);

    KNearestOcr ocr(config);
    if (! ocr.loadTrainingData()) {
        std::cout << "Failed to load OCR training data\n";
        return;
    }
    std::cout << "OCR training data loaded.\n";
    std::string path;

    while (pImageInput->nextImage(path) && !do_exit) {
        std::cout << "--------------=================------------" << std::endl;
        proc.setInput(pImageInput->getImage());
        proc.process();

        std::string result = ocr.recognize(proc.getOutput());

        if (result.find("?") != std::string::npos) {
            std::cout << "Unrecognized  " << result << std::endl;
            pImageInput->saveImage();
        }
        bool checked = plausi.check(result, pImageInput->getTime());
        double value = plausi.getCheckedValue();
        if (checked) {
            std::cout << "New  " << std::fixed << std::setprecision(3) << value << std::endl;
        } else {
            std::cout << "Old  " << std::fixed << std::setprecision(3) << value << std::endl;
        }
        if (value > 0) {
            mosq->publish_state(value);
        }
    }
}

static void learnOcr(ImageInput * pImageInput) {
    log4cpp::Category::getRoot().info("learnOcr");

    Config config;
    config.loadConfig();
    ImageProcessor proc(config);
    proc.debugWindow();
    proc.debugDigits();

    KNearestOcr ocr(config);
    ocr.loadTrainingData();
    std::cout << "Entering OCR training mode!\n";
    std::cout << "<0>..<9> to answer digit, <space> to ignore digit, <s> to save and quit, <q> to quit without saving.\n";

    int key = 0;
    std::string path;
    while (pImageInput->nextImage(path)) {
        proc.setInput(pImageInput->getImage());
        proc.process();

        std::string result = ocr.recognize(proc.getOutput());
        if (result.find("?") != std::string::npos) {
            proc.markBadDigits(result);
            std::cout << "Learn:" << result << "  " << std::endl;
            for(std::string::size_type i = 0; i < result.size(); ++i) {
                if (result[i] == '?') {
                    key = ocr.learn(proc.getOutput()[i]);
                    if (key == 'q') break;
                }
            }
            if (key == 'q') break;
        }
    }

    if (key != 'q' && ocr.hasTrainingData()) {
        std::cout << "Saving training data\n";
        ocr.saveTrainingData();
    }
}

static void adjustCamera(ImageInput * pImageInput) {
    log4cpp::Category::getRoot().info("adjustCamera");

    Config config;
    config.loadConfig();
    ImageProcessor proc(config);
    proc.debugWindow();
    proc.debugDigits();
    //proc.debugEdges();
    //proc.debugSkew();

    std::cout << "Adjust camera.\n";
    std::cout << "<r>, <p> to select raw or processed image, <s> to save config and quit, <q> to quit without saving.\n";

    bool processImage = true;
    int key = 0;
    std::string path;
    while (pImageInput->nextImage(path)) {
        proc.setInput(pImageInput->getImage());
        if (processImage) {
            proc.process();
        } else {
            proc.showImage();
        }

        key = cv::waitKey(delay) & 255;

        if (key == 'q' || key == 's') {
            std::cout << "Quit\n";
            break;
        } else if (key == 'r') {
            processImage = false;
        } else if (key == 'p') {
            processImage = true;
        }
    }
    if (key != 'q') {
        std::cout << "Saving config\n";
        config.saveConfig();
    }
}

static void capture(ImageInput * pImageInput) {
    log4cpp::Category::getRoot().info("capture");

    std::cout << "Capturing images into directory.\n";
    std::cout << "<Ctrl-C> to quit.\n";
    std::string path;
    while (pImageInput->nextImage(path)) {
        usleep(delay * 1000L);
    }
}

static void writeData(ImageInput * pImageInput) {
    log4cpp::Category::getRoot().info("writeData");

    Config config;
    config.loadConfig();
    ImageProcessor proc(config);

    Plausi plausi;

    RRDatabase rrd("emeter.rrd");

    struct stat st;

    KNearestOcr ocr(config);
    if (! ocr.loadTrainingData()) {
        std::cout << "Failed to load OCR training data\n";
        return;
    }
    std::cout << "OCR training data loaded.\n";
    std::cout << "<Ctrl-C> to quit.\n";
    std::string path;
    while (pImageInput->nextImage(path)) {
        proc.setInput(pImageInput->getImage());
        proc.process();

        if (proc.getOutput().size() == 7) {
            std::string result = ocr.recognize(proc.getOutput());
            if (plausi.check(result, pImageInput->getTime())) {
                rrd.update(plausi.getCheckedTime(), plausi.getCheckedValue());
            }
        }
        if (0 == stat("imgdebug", &st) && S_ISDIR(st.st_mode)) {
            // write debug image
            pImageInput->setOutputDir("imgdebug");
            pImageInput->saveImage();
            pImageInput->setOutputDir("");
        }
        usleep(delay * 1000L);
    }
}

static void usage(const char * progname) {
    std::cout << "Program to read and recognize the counter of an electricity meter with OpenCV.\n";
    std::cout << "Version: " << VERSION << std::endl;
    std::cout << "Usage: " << progname << " [-i <dir>|-c <cam>] [-l|-t|-a|-w|-o <dir>] [-s <delay>] [-v <level>\n";
    std::cout << "\nImage input:\n";
    std::cout << "  -i <image directory> : read image files (png) from directory.\n";
    std::cout << "  -c <camera number> : read images from camera.\n";
    std::cout << "\nOperation:\n";
    std::cout << "  -a : adjust camera.\n";
    std::cout << "  -o <directory> : capture images into directory.\n";
    std::cout << "  -l : learn OCR.\n";
    std::cout << "  -t : test OCR.\n";
    std::cout << "  -w : write OCR data to RR database. This is the normal working mode.\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -s <n> : Sleep n milliseconds after processing of each image (default=1000).\n";
    std::cout << "  -v <l> : Log level. One of DEBUG, INFO, ERROR (default).\n";
}

static void configureLogging(const std::string & priority = "INFO", bool toConsole = false) {
    log4cpp::Appender * fileAppender = new log4cpp::FileAppender("default", "emeocv.log");
    log4cpp::PatternLayout * layout = new log4cpp::PatternLayout();
    layout->setConversionPattern("%d{%d.%m.%Y %H:%M:%S} %p: %m%n");
    fileAppender->setLayout(layout);
    log4cpp::Category & root = log4cpp::Category::getRoot();
    root.setPriority(log4cpp::Priority::getPriorityValue(priority));
    root.addAppender(fileAppender);
    if (toConsole) {
        std::cout << "CONSOLE\n";
        log4cpp::Appender * consoleAppender = new log4cpp::OstreamAppender("console", &std::cout);
        consoleAppender->setLayout(new log4cpp::SimpleLayout());
        root.addAppender(consoleAppender);
    }
}

int main(int argc, char ** argv) {
    int opt;
    ImageInput * pImageInput = 0;
    int inputCount = 0;
    std::string outputDir;
    std::string logLevel = "ERROR";
    std::string hostname = "gas_reco";
    char cmd = 0;
    int cmdCount = 0;

    while ((opt = getopt(argc, argv, "i:c:ltaws:ov:hd:mx:H:")) != -1) {
        switch (opt) {
        case 'd':
            pImageInput = new InotifyInput(optarg, 100000);
            inputCount++;
            break;
        case 'i':
            pImageInput = new DirectoryInput(Directory(optarg, ".png"));
            inputCount++;
            break;
        case 'c':
            pImageInput = new CameraInput(atoi(optarg));
            inputCount++;
            break;
        case 'l':
        case 't':
        case 'a':
        case 'w':
        case 'm':
        case 'o':
            cmd = opt;
            cmdCount++;
            break;
        case 's':
            delay = atoi(optarg);
            break;
        case 'v':
            logLevel = optarg;
            break;
        case 'x':
            outputDir = optarg;
            break;
        case 'H':
            hostname = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }
    if (inputCount != 1) {
        std::cerr << "*** You should specify exactly one camera or input directory!\n\n";
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    if (cmdCount != 1) {
        std::cerr << "*** You should specify exactly one operation!\n\n";
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    configureLogging(logLevel, true);
    mosqpp::lib_init();
    mosquittoPP * mosq = new mosquittoPP("gas_reco", true, hostname.c_str());

    mosq->username_pw_set("owntracks", "zhopa");
    mosq->will_set(mosq->make_topic(TOPIC_LWT).c_str(), strlen(OFFLINE), OFFLINE, 0, true);
    mosq->connect(mqtt_host, mqtt_port, mqtt_keepalive);


    std::thread mosq_th(mosq_thread_loop, mosq);


    switch (cmd) {
    case 'o':
        pImageInput->setOutputDir(outputDir);
        capture(pImageInput);
        break;
    case 'l':
        learnOcr(pImageInput);
        break;
    case 'm':
        pImageInput->setOutputDir(outputDir);
        mqttOcr(pImageInput, mosq);
        break;
    case 't':
        testOcr(pImageInput);
        break;
    case 'a':
        adjustCamera(pImageInput);
        break;
    case 'w':
        writeData(pImageInput);
        break;
    }

    do_exit = true;
    delete pImageInput;
    mosq->publish_lwt(false);
    mosq->disconnect();
    mosq_th.join();
    delete mosq;
    mosqpp::lib_cleanup();
    exit(EXIT_SUCCESS);
}
