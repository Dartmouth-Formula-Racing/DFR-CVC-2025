/*
 * filter.h
 *
 * Created on January 7, 2025
 * Andrei Gerashchenko
 */
#ifndef CVC_FILTER_H
#define CVC_FILTER_H

#include <main.h>
#include <stdbool.h>

#define PI 3.1415926536

typedef struct {
    float omega;
    float alpha;

    // Filter coefficients
    float a0;
    float a1;
    float a2;
    float b0;
    float b1;
    float b2;

    // State variables
    float x1;
    float x2;
    float y1;
    float y2;
} notchfilter_t;

extern bool Inverter1_FilteredSpeed_Flag;
extern bool Inverter2_FilteredSpeed_Flag;

void Filter_InitializeNotch(notchfilter_t* filter, uint16_t samplerate, uint16_t center, uint16_t bandwidth);

int16_t Filter_ProcessNotch(notchfilter_t* filter, int16_t sample);

void Filter_InitializeFilters(void);

void Filter_ProcessFilterTask(void);

#endif  // CVC_FILTER_H