/*
 * Plausi.h
 *
 */

#ifndef PLAUSI_H_
#define PLAUSI_H_

#include <string>
#include <deque>
#include <utility>
#include <ctime>

class Plausi {
public:
    Plausi(double maxPower = 50. /*kW*/, size_t window = 13);
    bool check(const std::string & value, time_t time);
    double getCheckedValue();
    time_t getCheckedTime();
private:
    std::string queueAsString();
    double _maxPower;
    size_t _window;
    std::deque<std::pair<time_t, double> > _queue;
    double _value;
    time_t _time;
};

#endif /* PLAUSI_H_ */
