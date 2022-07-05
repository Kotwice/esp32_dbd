#include <Timer.h>

void Timer::update() {

    if (*current_state) {

        if(*current_time - previously_time > int(*duration)) {

            function();

            previously_time = *current_time;

        }

    }

}