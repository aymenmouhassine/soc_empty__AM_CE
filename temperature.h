#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

void temperature_init(void);             // Initialiser le capteur
uint32_t temperature_read(void);      // Lire la température en °C (ex: 32.45)

#endif // TEMPERATURE_H
