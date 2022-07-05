#include <Arduino.h>
#include <functional>

class Timer {

    public:

        Timer(uint32_t* CURRENT_TIME, float* DURATION, bool* CURRENT_STATE) : current_time(CURRENT_TIME), duration(DURATION), current_state(CURRENT_STATE) {};

        void update();

        std::function<void()> function = [] () {};

        uint32_t previously_time;

    private:

        uint32_t *current_time;
        
        float *duration;

        bool *current_state;

};