#include "fuel_monitor.h"

// Współczynnik wygładzania filtra EMA (0.3 oznacza 30% wagi dla nowego okrążenia)
#define EMA_ALPHA 0.3f 
#define PIT_EXIT_SPEED_THRESHOLD 5.0f

// Prywatne zmienne stanu (pamięć między pakietami)
static float s_fuel_previous = -1.0f;
static float s_fuel_at_lap_start = -1.0f;
static int s_lap_previous = -1;
static bool s_is_refueling = false;
static float s_consumption_per_lap = 0.0f;

/**
 * Inicjalizacja lub reset stanu (np. przed nowym wyścigiem)
 */
void fuel_monitor_init(void) {
    s_fuel_previous = -1.0f;
    s_fuel_at_lap_start = -1.0f;
    s_lap_previous = -1;
    s_is_refueling = false;
    s_consumption_per_lap = 0.0f;
}

/**
 * Główna funkcja obliczeniowa - wywoływana co każdy odebrany pakiet UDP (60Hz)
 */
void fuel_monitor_update(const GT7_Telemetry_t *telemetry, Fuel_Status_t *status) {
    // 1. Obsługa pierwszego uruchomienia i synchronizacja danych
    if (s_fuel_previous < 0.0f) {
        s_fuel_previous = telemetry->fuel_current;
        s_fuel_at_lap_start = telemetry->fuel_current;
        s_lap_previous = telemetry->lap_count;
        
        status->instantaneous_consumption = 0.0f;
        status->consumption_per_lap = 0.0f;
        status->laps_remaining = -1.0f; // -1 oznacza brak danych (pierwsze kółko)
        status->is_refueling = false;
        return;
    }

    // =================================================================
    // KROK 1: DETEKCJA PIT-STOPU (DOLEWKI)
    // =================================================================
    if (telemetry->fuel_current > s_fuel_previous) {
        s_is_refueling = true;
        
        // Podczas tankowania zamrażamy obliczenia zużycia
        status->is_refueling = true;
        status->instantaneous_consumption = 0.0f;
        
        s_fuel_previous = telemetry->fuel_current;
        return; // Przerywamy przetwarzanie tego pakietu
    }

    // Wykrywanie wyjazdu z pit-stopu (paliwo przestało rosnąć i auto jedzie)
    if (s_is_refueling && (telemetry->fuel_current <= s_fuel_previous)) {
        if (telemetry->speed_kmh > PIT_EXIT_SPEED_THRESHOLD) {
            s_is_refueling = false;
            // Resetujemy punkt startowy dla nowego okrążenia po tankowaniu
            s_fuel_at_lap_start = telemetry->fuel_current; 
        }
    }

    // =================================================================
    // KROK 2: ZUŻYCIE CHWILOWE
    // =================================================================
    float delta_fuel = s_fuel_previous - telemetry->fuel_current;
    if (delta_fuel >= 0.0f) {
        // Mnożnik 60.0f, ponieważ pakiety z GT7 przychodzą z częstotliwością 60Hz
        status->instantaneous_consumption = delta_fuel * 60.0f;
    } else {
        status->instantaneous_consumption = 0.0f;
    }

    // =================================================================
    // KROK 3: ZUŻYCIE PER LAP (ZMIANA OKRĄŻENIA)
    // =================================================================
    if (telemetry->lap_count > s_lap_previous) {
        // Obliczamy zużycie tylko, jeśli mamy poprawny punkt startowy z poprzedniego okrążenia
        if (s_fuel_at_lap_start > 0.0f && s_fuel_at_lap_start > telemetry->fuel_current) {
            float exact_fuel_this_lap = s_fuel_at_lap_start - telemetry->fuel_current;

            // Filtrowanie EMA (Exponential Moving Average)
            if (s_consumption_per_lap == 0.0f) {
                s_consumption_per_lap = exact_fuel_this_lap; // Pierwsze pełne okrążenie
            } else {
                s_consumption_per_lap = (s_consumption_per_lap * (1.0f - EMA_ALPHA)) + (exact_fuel_this_lap * EMA_ALPHA);
            }
        }
        // Ustawiamy bazę pod nowe okrążenie
        s_fuel_at_lap_start = telemetry->fuel_current;
        s_lap_previous = telemetry->lap_count;
    }

    // =================================================================
    // KROK 4: PROGNOZOWANIE (LAPS REMAINING)
    // =================================================================
    if (s_consumption_per_lap > 0.0f) {
        status->laps_remaining = telemetry->fuel_current / s_consumption_per_lap;
    } else {
        status->laps_remaining = -1.0f; // Status "CALC" na pierwszym okrążeniu
    }

    // Aktualizacja stanu i wyjścia
    status->consumption_per_lap = s_consumption_per_lap;
    status->is_refueling = s_is_refueling;
    s_fuel_previous = telemetry->fuel_current;
}