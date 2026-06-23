#include <stdio.h>
#include "fuel_monitor.h"

// Instancje struktur
GT7_Telemetry_t live_telemetry;
Fuel_Status_t fuel_results;

void app_main(void) {
    // Inicjalizacja kalkulatora paliwa
    fuel_monitor_init();

    while (1) {
        // [TUTAJ: Twój istniejący kod odbierający pakiet UDP]
        // Przykładowe mapowanie odebranych danych do struktury:
        // live_telemetry.fuel_current = udp_packet.fuel_amount;
        // live_telemetry.lap_count = udp_packet.lap_number;
        // live_telemetry.speed_kmh = udp_packet.speed * 3.6f;

        // Przetworzenie danych przez procedurę
        fuel_monitor_update(&live_telemetry, &fuel_results);

        // Wyświetlanie danych na ekranie OLED/TFT lub w konsoli VS
        if (fuel_results.is_refueling) {
            printf("Status: TANKOWANIE... | Paliwo: %.1f%%\n", live_telemetry.fuel_current);
        } else {
            printf("Paliwo: %.1f%% | Chwilowe: %.2f/s | Per Lap: %.2f | Pozostało okrążeń: ", 
                   live_telemetry.fuel_current, 
                   fuel_results.instantaneous_consumption, 
                   fuel_results.consumption_per_lap);
            
            if (fuel_results.laps_remaining < 0.0f) {
                printf("CALC\n");
            } else {
                printf("%.2f\n", fuel_results.laps_remaining);
            }
        }

        // vTaskDelay lub mechanizm czekający na następny pakiet UDP
    }
}