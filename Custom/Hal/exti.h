#ifdef __cplusplus
extern "C" {
#endif
#include "main.h"

#ifndef EXTI_H_
#define EXTI_H_


void exti0_irq_unregister(void *f);

void exti0_irq_register(void *f);
void exti2_irq_register(void *f);
void exti3_irq_register(void *f);
void exti4_irq_register(void *f);
void exti5_irq_register(void *f);
void exti8_irq_register(void *f);
void exti10_irq_register(void *f);
void exti11_irq_register(void *f);
void exti12_irq_register(void *f);
void exti13_irq_register(void *f);
void exti15_irq_register(void *f);

void exti0_all_callback(void);
void exti2_all_callback(void);
void exti3_all_callback(void);
void exti4_all_callback(void);
void exti5_all_callback(void);
void exti8_all_callback(void);
void exti10_all_callback(void);
void exti11_all_callback(void);
void exti12_all_callback(void);
void exti13_all_callback(void);
void exti15_all_callback(void);
#endif
