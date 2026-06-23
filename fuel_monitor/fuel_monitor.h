#ifndef FUEL_MONITOR_H
#define FUEL_MONITOR_H

#include <stdbool.h>

// Struktura wejściowa - dane bezpośrednio z telemetrii GT7
typedef struct {
    float fuel_current;   // Bieżąca ilość paliwa w baku (z pakietu)
    int lap_count;        // Bieżący numer okrążenia (z pakietu)
    float speed_kmh;      // Bieżąca prędkość w km/h (potrzebna do wyjazdu z pitstopu)
} GT7_Telemetry_t;

// Struktura wyjściowa - wyniki dla Twojego wyświetlacza
typedef struct {
    float instantaneous_consumption; // Zużycie chwilowe (%/s lub L/s)
    float consumption_per_lap;       // Średnie wygładzone zużycie na okrążenie
    float laps_remaining;            // Prognoza (na ile okrążeń zostało)
    bool is_refueling;               // Flaga: czy trwa tankowanie
} Fuel_Status_t;

// Funkcje interfejsu
void fuel_monitor_init(void);
void fuel_monitor_update(const GT7_Telemetry_t *telemetry, Fuel_Status_t *status);

#endif // FUEL_MONITOR_H