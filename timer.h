#ifndef TIMER_H
#define TIMER_H
#include<time.h>
#include <chrono>
#include<queue>
#include <map>
#include <thread>
#include<vector>
#include <mutex>
using std::chrono::high_resolution_clock;
using std::chrono::nanoseconds;
using std::chrono::duration;
using std::chrono::duration_cast;
class Timer{
public:
    double interval;
    bool valid;
    high_resolution_clock::time_point timeout;
    Timer(double interval):interval(interval)
    {
        valid = false;
        timeout = high_resolution_clock::now();
    }
    void start() //not run
    {
        if(!valid) valid = true;
        auto now = high_resolution_clock::now();
        timeout = now > timeout ? now : timeout;
    }
    void pause() //running
    {
        valid = false;
    }
    void updateTimeOut() //running
    {
        //cout << "timeout:" << interval << endl;
        timeout = high_resolution_clock::now() + 
		   duration_cast<nanoseconds>(duration<double>(interval));
    }
    void updateInterval(double _interval) //running or not run + start
    {
        //cout << "interval:" << _interval << endl;
        // this->timeout += duration_cast<nanoseconds>(duration<double>(_interval - this->interval));
        this->interval = _interval;   
    }
    bool isTimeOut() //running
    {
        return high_resolution_clock::now() >= timeout;
    }
};
#endif // TIMER_H
