#include "sensor.h"
#include <stdlib.h>  // Para rand()
#include <time.h>    // Para time()

static int initialized = 0;

void sensor_init(void) {
    if (!initialized) {
        srand(time(NULL));  // Inicializa la semilla del RNG
        initialized = 1;
    }
}

double sensor_read(void) {
    // Retorna un valor aleatorio entre 0 y 100
    return (double)(rand() % 101);
}
