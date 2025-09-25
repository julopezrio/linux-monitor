#include <stdio.h>
#include "sensor/sensor.h"

int main(void) {
    sensor_init();
    for (int i = 0; i < 10; i++){
        double val = sensor_read();
        printf("Sensor reading %d: %f\n", i, val);
    }
    return 0;
}
