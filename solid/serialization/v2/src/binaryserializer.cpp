
#include "solid/serialization/v2/binaryserializer.hpp"
#include "solid/serialization/v2/binarybasic.hpp"
#include "solid/system/exception.hpp"

namespace solid {
namespace serialization {
namespace v2 {
namespace binary {

//== Serializer  ==============================================================
SerializerBase::SerializerBase()
    : pbeg_(nullptr)
    , pend_(nullptr)
    , pcrt_(nullptr)
    , sentinel_(InvalidIndex())
    , run_lst_(run_vec_)
    , cache_lst_(run_vec_)
{
}

std::ostream& SerializerBase::run(std::ostream& _ros)
{
    const size_t buf_cap = 8 * 1024;
    char         buf[buf_cap];
    long         len;

    while ((len = run(buf, buf_cap)) > 0) {
        _ros.write(buf, len);
    }
    return _ros;
}

long SerializerBase::run(char* _pbeg, unsigned _sz, void* _pctx)
{
    pbeg_ = _pbeg;
    pend_ = _pbeg + _sz;
    pcrt_ = _pbeg;

    while (not run_lst_.empty()) {
        idbg("run frontIndex = " << run_lst_.frontIndex());
        Runnable&     rr = run_lst_.front();
        const ReturnE rv = rr.call_(*this, rr, _pctx);
        switch (rv) {
        case ReturnE::Done:
            rr.clear();
            idbg("done frontIndex = " << run_lst_.frontIndex());
            cache_lst_.pushBack(run_lst_.popFront());
            break;
        case ReturnE::Continue:
            break;
        case ReturnE::Wait:
            goto DONE;
        }
    }
DONE:
    long rv = error_ ? -1 : pcrt_ - pbeg_;
    pcrt_ = pbeg_ = pend_ = nullptr;
    return rv;
}

void SerializerBase::clear()
{
    run_lst_.clear();
    run_vec_.clear();
}

size_t SerializerBase::schedule(Runnable&& _ur)
{
    size_t idx;
    if (cache_lst_.size()) {
        idx           = cache_lst_.popFront();
        run_vec_[idx] = std::move(_ur);
    } else {
        idx = run_vec_.size();
        run_vec_.emplace_back(std::move(_ur));
    }

    if (sentinel_ == InvalidIndex()) {
        idbg("pushBack= " << idx);
        run_lst_.pushBack(idx);
    } else {
        //insert in front of the setinel
        idbg("insert " << idx << " in front of " << sentinel_);
        run_lst_.insertFront(sentinel_, idx);
    }
    return idx;
}

void SerializerBase::tryRun(Runnable&& _ur, void* _pctx)
{
    size_t idx = schedule(std::move(_ur));

    if (idx == run_lst_.frontIndex()) {
        //we try run the function on spot
        Runnable& rr = run_lst_.front();
        ReturnE   v  = rr.call_(*this, rr, _pctx);
        if (v == ReturnE::Done) {
            rr.clear();
            cache_lst_.pushBack(run_lst_.popFront());
        }
    }
}

void SerializerBase::limits(const Limits& _rlimits)
{
    idbg("");
    if (isRunEmpty()) {
        limits_ = _rlimits;
    } else {
        Runnable r{
            nullptr,
            call_function,
            [_rlimits](SerializerBase& _rs, Runnable& /*_rr*/, void* /*_pctx*/) {
                _rs.limits_ = _rlimits;
                return Base::ReturnE::Done;
            },
            ""};
        schedule(std::move(r));
    }
}

//-- store functions ----------------------------------------------------------

Base::ReturnE SerializerBase::store_byte(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{
    return _rs.doStoreByte(_rr);
}

Base::ReturnE SerializerBase::store_cross(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{

    return _rs.doStoreCross(_rr);
}

Base::ReturnE SerializerBase::store_cross_with_check(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{
    return _rs.doStoreCrossWithCheck(_rr);
}

Base::ReturnE SerializerBase::store_binary(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{
    return _rs.doStoreBinary(_rr);
}

Base::ReturnE SerializerBase::call_function(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{
    return _rr.fnc_(_rs, _rr, _pctx);
}

Base::ReturnE SerializerBase::noop(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{
    return ReturnE::Done;
}

Base::ReturnE SerializerBase::store_stream(SerializerBase& _rs, Runnable& _rr, void* _pctx)
{
    std::istream& ris    = *const_cast<std::istream*>(static_cast<const std::istream*>(_rr.ptr_));
    size_t        toread = _rs.pend_ - _rs.pcrt_;

    if (toread > 2) {
        toread -= 2;
        if (_rr.size_ != InvalidSize()) {
            if (_rr.size_ < toread) {
                toread = _rr.size_;
            }
        }

        if (ris) {
            ris.read(_rs.pcrt_ + 2, toread);
            toread = ris.gcount();
        } else {
            toread = 0;
        }
        SOLID_CHECK(toread <= 0xffff);
        uint16_t chunk_len = static_cast<uint16_t>(toread);
        _rs.pcrt_          = store(_rs.pcrt_, chunk_len);
        _rs.pcrt_ += toread;
        _rr.data_ += toread;

        if (_rs.limits().hasStream() && _rr.data_ > _rs.limits().stream()) {
            _rs.error(error_limit_stream);
            return ReturnE::Done;
        }

        bool done = (toread == 0); //we need to have written a final chunk_len == 0

        if (_rr.size_ != InvalidSize()) {
            _rr.size_ -= toread;
        }

        if (!done) {
            _rr.fnc_(_rs, _rr, _pctx);
            return ReturnE::Wait;
        } else {
            _rr.size_ = 0;
            _rr.fnc_(_rs, _rr, _pctx);
        }
    } else {
        return ReturnE::Wait;
    }
    return ReturnE::Done;
}

} //namespace binary
} //namespace v2
} //namespace serialization
} //namespace solid
