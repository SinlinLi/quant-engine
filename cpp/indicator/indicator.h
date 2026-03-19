#pragma once

#include "data/bar.h"

namespace qe {

class Indicator {
public:
    virtual ~Indicator() = default;
    virtual void update(const Bar& bar) = 0;
    virtual double value() const = 0;
    virtual bool ready() const = 0;
};

}  // namespace qe
