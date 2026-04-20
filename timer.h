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
    nanoseconds interval;
    bool valid;
    high_resolution_clock::time_point timeout;
    high_resolution_clock::time_point last_check_pass;
    Timer(double interval)
    {
        this->interval = duration_cast<nanoseconds>(duration<double>(interval));
        valid = false;
        timeout = high_resolution_clock::now();
        last_check_pass = timeout;
    }
    void start() //not run
    {
        if(!valid) valid = true;
        auto now = high_resolution_clock::now();
        timeout = now > timeout ? now : timeout;
        last_check_pass = timeout;
    }
    void pause() //running
    {
        valid = false;
    }
    void updateTimeOut() //running
    {
        //cout << "timeout:" << interval << endl;
        timeout += this->interval;
    }
    void updateInterval(double _interval) //running or not run + start
    {
        //cout << "interval:" << _interval << endl;
        // this->timeout += duration_cast<nanoseconds>(duration<double>(_interval - this->interval));
        this->interval = duration_cast<nanoseconds>(duration<double>(_interval)); 
    }
    bool isTimeOut() //running
    {
        return high_resolution_clock::now() >= timeout;
        // auto t = high_resolution_clock::now();
        // if(t >= timeout)
        // {
        //     // // printf("interval is %ld\n", uint32_t(this->interval * 1e9));
        //     // // printf("cur time is : %ld\n", t.time_since_epoch().count());
        //     // // printf("timeout is : %ld\n", timeout.time_since_epoch().count());
        //     // printf("diff is %lf\n", duration_cast<duration<double>>(t - timeout).count());
        //     // last_check_pass = t;
        //     return true;
        // }
        // return false;
    }
};
#endif // TIMER_H
