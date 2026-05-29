#ifndef __APP_H753_H
#define __APP_H753_H

/*================ 运行模式 ================*/
#define APP_MODE  0  /* 0=模拟模式, 1=真实模式 */

void App_Init(void);
void App_Run(void);

/* Screen_Task 由 screen.h 声明，screen.c 实现 */

#endif
