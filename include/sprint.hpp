#ifndef TWINE_SPRINT_HPP
#define TWINE_SPRINT_HPP

#include <cstdint>

namespace twine::sprint {

class InputPublication {
public:
    explicit InputPublication(int player) : player_(player) {}
    ~InputPublication();
    InputPublication(const InputPublication&) = delete;
    InputPublication& operator=(const InputPublication&) = delete;
    bool held = false;
    bool dedicated = false;
    bool forward = false;
private:
    int player_;
};

void begin_tick();
}

#endif
