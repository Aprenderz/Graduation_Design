#ifndef __BATTERY_H__
#define __BATTERY_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float voltage;       // V
    bool valid;
} BatteryData_t;

BatteryData_t Battery_Read(void);

#endif /* __BATTERY_H__ */

