#ifndef __APP_H753_H
#define __APP_H753_H

/*================ 运行模式 ================*/
#define APP_MODE  0  /* 0=模拟模式, 1=真实模式 */

void App_Init(void);
void App_Run(void);

/*================ 占位函数 — 真实模式下逐步实现 ================*/
void Screen_Task(void);  /* 串口屏任务（待实现）*/

#endif
