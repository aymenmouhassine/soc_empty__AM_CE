/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "app.h"
#include "app_log.h"
#include "sl_sensor_rht.h"
#include "temperature.h"
#include "gatt_db.h"
#include <string.h> // pour memcpy
#include "sl_bt_api.h"
#include "sl_sleeptimer.h"
#include "sl_simple_led_instances.h"



#define TEMPERATURE_TIMER_SIGNAL (1 << 0)

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
uint32_t rh;
int32_t t;
static sl_sleeptimer_timer_handle_t temperature_timer;
static uint32_t timer_counter = 0;
static uint8_t connection_handle = 0xff; // Valeur invalide au début
extern const sl_led_t sl_led_led0;




/**************************************************************************//**
 * Application Init.
 *****************************************************************************/
SL_WEAK void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////
  app_log_info("%s\n", __FUNCTION__);


}

/**************************************************************************//**
 * Application Process Action.
 *****************************************************************************/
SL_WEAK void app_process_action(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application code here!                              //
  // This is called infinitely.                                              //
  // Do not call blocking functions from here!                               //
  /////////////////////////////////////////////////////////////////////////////
}
void temperature_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
    (void)handle;
    (void)data;

    timer_counter++;
    app_log_info("%s: Timer step %lu\n", __FUNCTION__, timer_counter);
    sl_bt_external_signal(TEMPERATURE_TIMER_SIGNAL);

}


/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the dummy weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);
      // Start advertising and enable connections.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      app_log_info("%s: connection_opened!\n",__FUNCTION__);
      break;

      //This event indicates that the temperature has been requested
    case sl_bt_evt_gatt_server_user_read_request_id:
    {
        uint16_t char_id = evt->data.evt_gatt_server_user_read_request.characteristic;

        if (char_id == gattdb_temperature_0) {
            temperature_init(); // Initialise le capteur
            uint32_t temp = temperature_read() / 10;  // Ex: 324 -> 32°C
            uint8_t buffer[2];
            memcpy(buffer, &temp, sizeof(uint16_t)); // Copie la température dans le buffer

            sl_status_t sc = sl_bt_gatt_server_send_user_read_response(
                evt->data.evt_gatt_server_user_read_request.connection, // Connexion
                char_id,                                                // Caractéristique
                SL_STATUS_OK,                                           // Code OK (0x00)
                sizeof(buffer),                                         // Taille des données
                buffer,                                                 // Données envoyées
                NULL                                                    // Taille réellement envoyée (facultative ici)
            );

            app_log_info("%s: Température envoyée = %lu °C\n", __FUNCTION__, temp);
            app_assert_status(sc); // Log si erreur
        } else {
            app_log_info("%s: Lecture d'une autre caractéristique ID = %d\n", __FUNCTION__, char_id);
        }
    }
    break;


    case sl_bt_evt_gatt_server_characteristic_status_id: {
        sl_bt_evt_gatt_server_characteristic_status_t status = evt->data.evt_gatt_server_characteristic_status;

        if (status.characteristic == gattdb_temperature_0 &&
            (status.status_flags & sl_bt_gatt_server_client_config)) {

            if (status.client_config_flags == sl_bt_gatt_server_notification) {

                connection_handle = status.connection;

                app_log_info("%s: Notify ACTIVATED — Starting timer\n", __FUNCTION__);
                sl_sleeptimer_start_periodic_timer_ms(
                    &temperature_timer,
                    1000, // 1 Hz = 1000 ms
                    temperature_timer_callback,
                    NULL, // pas de contexte pour l'instant
                    0,
                    0
                );
            } else {
                app_log_info("%s: Notify DEACTIVATED — Stopping timer\n", __FUNCTION__);
                sl_sleeptimer_stop_timer(&temperature_timer);
            }
        }
        break;
    }

    case sl_bt_evt_system_external_signal_id: {
        if (evt->data.evt_system_external_signal.extsignals & TEMPERATURE_TIMER_SIGNAL) {


             temperature_init(); // Initialise le capteur
             uint32_t temp = temperature_read() / 10;  // Ex: 324 -> 32°C
             uint8_t buffer[2];
             memcpy(buffer, &temp, sizeof(uint16_t)); // Copie la température dans le buffer

            sl_status_t sc = sl_bt_gatt_server_send_notification(
                connection_handle,
                gattdb_temperature_0,
                sizeof(buffer),
                buffer
            );

            if (sc == SL_STATUS_OK) {
                app_log_info("%s: Température envoyée = %lu °C\n", __FUNCTION__, temp);
            } else {
                app_log_info("%s: Error sending temperature: 0x%04x\n", __FUNCTION__, (unsigned int)sc);
            }
        }
        break;
    }


   /* case sl_bt_evt_gatt_server_characteristic_status_id: {
        sl_bt_evt_gatt_server_characteristic_status_t status = evt->data.evt_gatt_server_characteristic_status;

        app_log_info("%s: Characteristic handle: 0x%04x\n", __FUNCTION__, status.characteristic);
        app_log_info("%s: Status flags: 0x%02x\n", __FUNCTION__, status.status_flags);
        app_log_info("%s: Client config flags: 0x%02x\n", __FUNCTION__, status.client_config_flags);



        if (status.characteristic == gattdb_temperature_0 &&
            (status.status_flags & sl_bt_gatt_server_client_config)) {

            if (status.client_config_flags == sl_bt_gatt_server_notification) {
                app_log_info("%s: Notify ACTIVATED for Temperature\n", __FUNCTION__);
            } else {
                app_log_info("%s: Notify DEACTIVATED for Temperature\n", __FUNCTION__);
            }
        }
        break;
    }*/

    /*case sl_bt_evt_gatt_server_characteristic_status_id :

      sl_bt_evt_gatt_server_characteristic_status_t status = evt->data.evt_gatt_server_characteristic_status;

          if (status.characteristic == gattdb_temperature_0 &&
              (status.status_flags & sl_bt_gatt_server_client_config)) {
              if (status.client_config_flags == sl_bt_gatt_server_notification) {
                  app_log_info("%s: Notify ENABLED for Temperature\n", __FUNCTION__);

              } else {
                  app_log_info("%s: Notify DISABLED for Temperature\n", __FUNCTION__);

              }
          }
          break;*/


      /*sl_sleeptimer_init();
      app_log_info("%s: NOTIFY !!!! \n", __FUNCTION__ );
      uint16_t char_id = evt->data.evt_gatt_server_user_read_request.characteristic;
      if (char_id == gattdb_temperature_0) {
          app_log_info("%s: C'est bien la caracteristique temperature \n", __FUNCTION__ );
          if( sl_bt_evt_gatt_server_characteristic_status_t){
              app_log_info("%s: Status flag egal a 1 : active \n", __FUNCTION__ );
          }
          else{
              app_log_info("%s: Status flag egal a 0 : desactive \n", __FUNCTION__ );
          }

      }
      else {
                  app_log_info("%s: Lecture d'une autre caractéristique ID = %d\n", __FUNCTION__, char_id);
              }


      break ;*/

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:

      app_log_info("%s: Notify DEACTIVATED — Stopping timer\n", __FUNCTION__);
      sl_sleeptimer_stop_timer(&temperature_timer);


              // Generate data for advertising
                    app_log_info("%s: connection_closed!\n",__FUNCTION__);
                    sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                               sl_bt_advertiser_general_discoverable);
                    app_assert_status(sc);


      // Restart advertising after client has disconnected.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      break;



    ///////////////////////////////////////////////////////////////////////////
    // Add additional event handlers here as your application requires!      //
    ///////////////////////////////////////////////////////////////////////////

    // -------------------------------
    // Default event handler.
    default:
      break;
  }
}
