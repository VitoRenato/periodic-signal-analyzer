#ifndef APP_MEASUREMENT_H
#define APP_MEASUREMENT_H

/* 生产主循环的单步入口；硬件初始化仍由 main() 负责。 */
void App_MeasurementInit(void);
void App_MeasurementLoopOnce(void);

#endif
