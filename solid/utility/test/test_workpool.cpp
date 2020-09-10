#include "solid/system/crashhandler.hpp"
#include "solid/system/exception.hpp"
#include "solid/utility/workpool.hpp"
#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

using namespace solid;
using namespace std;

namespace {
std::atomic<size_t> val{0};
}

struct Context {
    deque<size_t>& rgdq_;
    mutex&         rgmtx_;
    deque<size_t>  ldq_;

    Context(deque<size_t>& _rgdq, mutex& _rgmtx)
        : rgdq_(_rgdq)
        , rgmtx_(_rgmtx)
    {
    }

    ~Context()
    {
        lock_guard<mutex> lock(rgmtx_);
        rgdq_.insert(rgdq_.end(), ldq_.begin(), ldq_.end());
    }
};

//#define EXTRA_CHECKING

int test_workpool(int argc, char* argv[])
{
    install_crash_handler();

    solid::log_start(std::cerr, {".*:VIEWXS"});

    cout << "usage: " << argv[0] << " JOB_COUNT WAIT_SECONDS QUEUE_SIZE PRODUCER_COUNT CONSUMER_COUNT PUSH_SLEEP_MSECS JOB_SLEEP_MSECS" << endl;
    using WorkPoolT  = WorkPool<size_t>;
    using AtomicPWPT = std::atomic<WorkPoolT*>;

    size_t        job_count        = 5000000;
    int           wait_seconds     = 100;
    int           queue_size       = -1;
    int           producer_count   = 0;
    int           consumer_count   = thread::hardware_concurrency();
    int           push_sleep_msecs = 0;
    int           job_sleep_msecs  = 0;
    deque<size_t> gdq;
    std::mutex    gmtx;
    AtomicPWPT    pwp{nullptr};

    if (argc > 1) {
        job_count = atoi(argv[1]);
    }

    if (argc > 2) {
        wait_seconds = atoi(argv[2]);
    }

    if (argc > 3) {
        queue_size = atoi(argv[3]);
    }

    if (argc > 4) {
        producer_count = atoi(argv[4]);
    }

    if (argc > 5) {
        consumer_count = atoi(argv[5]);
    }

    if (argc > 6) {
        push_sleep_msecs = atoi(argv[6]);
    }

    if (argc > 7) {
        job_sleep_msecs = atoi(argv[7]);
    }

    auto lambda = [&]() {
        WorkPool<size_t> wp{
            WorkPoolConfiguration(consumer_count, queue_size <= 0 ? std::numeric_limits<size_t>::max() : queue_size),
            1,
            [job_sleep_msecs](size_t _v, Context&& _rctx) {
                //solid_check(_rs == "this is a string", "failed string check");
                val += _v;
                if (job_sleep_msecs != 0) {
                    this_thread::sleep_for(chrono::milliseconds(job_sleep_msecs));
                }
#ifdef EXTRA_CHECKING
                _rctx.ldq_.emplace_back(_v);
#endif
            },
            Context(gdq, gmtx)};

        pwp = &wp;

        auto producer_lambda = [job_count, push_sleep_msecs, &wp]() {
            for (size_t i = 0; i < job_count; ++i) {
                if (push_sleep_msecs != 0) {
                    this_thread::sleep_for(chrono::milliseconds(push_sleep_msecs));
                }
                wp.push(i);
            };
        };

        if (producer_count != 0) {
            vector<thread> thr_vec;
            thr_vec.reserve(producer_count);
            for (int i = 0; i < producer_count; ++i) {
                thr_vec.emplace_back(producer_lambda);
            }
            for (auto& t : thr_vec) {
                t.join();
            }
        } else {
            producer_lambda();
        }
        pwp = nullptr;
    };
    auto fut = async(launch::async, lambda);
    if (fut.wait_for(chrono::seconds(wait_seconds)) != future_status::ready) {
        if (pwp != nullptr) {
            pwp.load()->dumpStatistics();
        }
        solid_throw(" Test is taking too long - waited " << wait_seconds << " secs");
    }
    const size_t v = (((job_count - 1) * job_count) / 2) * (producer_count == 0 ? 1 : producer_count);

    solid_log(generic_logger, Warning, "val = " << val << " expected val = " << v);

#ifdef EXTRA_CHECKING
    sort(gdq.begin(), gdq.end());

    const size_t  dv1 = -1;
    const size_t  dv2 = -1;
    const size_t* pv1 = &dv1;
    const size_t* pv2 = &dv2;

    for (size_t i = 0; i < gdq.size() - 1; i += 2) {
        const auto& cv1 = gdq[i];
        const auto& cv2 = gdq[i + 1];
        if (cv1 == *pv1) {
            cout << cv1 << '-';
        }
        if (cv2 == *pv2) {
            cout << cv2 << '+';
        }
        pv1 = &cv1;
        pv2 = &cv2;
    }

    //     for (const auto& cv : gdq) {
    //         if (cv != *pv) {
    //             cout << cv << ' ';
    //         }
    //         pv = &cv;
    //     }
    cout << endl;
#endif
    solid_check(v == val, v << " != " << val);
    return 0;
}
