#include "temperature.h"
#include "sl_sensor_rht.h"
#include "app_log.h"

static uint32_t humidity;
static int32_t temperature_celsius;

void temperature_init(void) {
  sl_sensor_rht_init();  // Active le capteur
}

uint32_t temperature_read(void) {
  sl_sensor_rht_get(&humidity, &temperature_celsius); // millidegrees
  app_log_info("%s,temperature = %ld",__FUNCTION__, (long)temperature_celsius);
  sl_sensor_rht_deinit(); // Désactive le capteur après lecture

  // Convertir milli°C → °C float
  return temperature_celsius ;
}
