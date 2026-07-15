/* Clock, cache, and timebase setup for the STM32H7B0 benchmark harness.
 *
 * Target configuration (matches WeAct/EC-Buying H7B0 boards, RM0455):
 *   LDO supply, VOS0, HSE 25 MHz crystal, PLL1 M=5 N=112 P=2 -> 280 MHz
 *   SYSCLK/CPU/AXI, /2 APB prescalers (140 MHz), flash latency 7 + WRHIGHFREQ,
 *   I-cache + D-cache enabled. TIM2 (32-bit, 280 MHz kernel clock) provides
 *   the microsecond timebase.
 *
 * If the HSE or PLL fails to come up the harness continues on the 64 MHz HSI
 * so a broken crystal still produces a report; tamp_system_report() prints the
 * achieved configuration so the runner output records what actually ran. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "stm32h7xx.h"

#define HSE_HZ 25000000u
#define TARGET_SYSCLK_HZ 280000000u
#define HSI_SYSCLK_HZ 64000000u

uint32_t SystemCoreClock = HSI_SYSCLK_HZ;

static bool clock_280mhz_ok;

static bool wait_flag(volatile const uint32_t *reg, uint32_t mask, uint32_t spins) {
    while (spins--) {
        if (*reg & mask) return true;
    }
    return false;
}

static bool clock_init_280mhz(void) {
    /* Supply: LDO (these boards power VCORE from the internal LDO). SCUEN +
     * LDOEN matches HAL's PWR_LDO_SUPPLY for the SMPS-less H7A3/B0/B3 line. */
    PWR->CR3 = (PWR->CR3 & ~PWR_CR3_BYPASS) | PWR_CR3_SCUEN | PWR_CR3_LDOEN;
    if (!wait_flag(&PWR->CSR1, PWR_CSR1_ACTVOSRDY, 1000000)) return false;

    /* VOS0 is required for 280 MHz. */
    PWR->SRDCR |= PWR_SRDCR_VOS;
    if (!wait_flag(&PWR->SRDCR, PWR_SRDCR_VOSRDY, 1000000)) return false;

    RCC->CR |= RCC_CR_HSEON;
    if (!wait_flag(&RCC->CR, RCC_CR_HSERDY, 1000000)) return false;

    /* PLL1: 25 MHz / M=5 = 5 MHz ref (RGE 4-8 MHz, wide VCO), * N=112 =
     * 560 MHz VCO, / P=2 = 280 MHz. */
    RCC->PLLCKSELR = (RCC->PLLCKSELR & ~(RCC_PLLCKSELR_PLLSRC | RCC_PLLCKSELR_DIVM1)) |
                     (5u << RCC_PLLCKSELR_DIVM1_Pos) | RCC_PLLCKSELR_PLLSRC_HSE;
    RCC->PLLCFGR = (RCC->PLLCFGR & ~(RCC_PLLCFGR_PLL1RGE | RCC_PLLCFGR_PLL1VCOSEL | RCC_PLLCFGR_PLL1FRACEN)) |
                   RCC_PLLCFGR_PLL1RGE_2 | RCC_PLLCFGR_DIVP1EN;
    RCC->PLL1DIVR = ((112u - 1u) << RCC_PLL1DIVR_N1_Pos) | ((2u - 1u) << RCC_PLL1DIVR_P1_Pos) |
                    ((2u - 1u) << RCC_PLL1DIVR_Q1_Pos) | ((2u - 1u) << RCC_PLL1DIVR_R1_Pos);
    RCC->PLL1FRACR = 0;
    RCC->CR |= RCC_CR_PLL1ON;
    if (!wait_flag(&RCC->CR, RCC_CR_PLL1RDY, 1000000)) return false;

    /* Flash wait states before raising the clock (RM0455; matches the vendor
     * FLASH_LATENCY_7 configuration for 280 MHz VOS0). */
    FLASH->ACR = FLASH_ACR_WRHIGHFREQ | FLASH_ACR_LATENCY_7WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_7WS)
        ;

    /* CPU/AXI/AHB at sysclk, all APB buses /2 (140 MHz max). */
    RCC->CDCFGR1 =
        (RCC->CDCFGR1 & ~(RCC_CDCFGR1_CDCPRE | RCC_CDCFGR1_HPRE | RCC_CDCFGR1_CDPPRE)) | RCC_CDCFGR1_CDPPRE_DIV2;
    RCC->CDCFGR2 = (RCC->CDCFGR2 & ~(RCC_CDCFGR2_CDPPRE1 | RCC_CDCFGR2_CDPPRE2)) | RCC_CDCFGR2_CDPPRE1_DIV2 |
                   RCC_CDCFGR2_CDPPRE2_DIV2;
    RCC->SRDCFGR = (RCC->SRDCFGR & ~RCC_SRDCFGR_SRDPPRE) | RCC_SRDCFGR_SRDPPRE_DIV2;

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL1;
    for (uint32_t spins = 1000000; spins; spins--) {
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL1) {
            SystemCoreClock = TARGET_SYSCLK_HZ;
            return true;
        }
    }
    return false;
}

/* TIM2 is a 32-bit timer on APB1; with APB prescaler /2 its kernel clock is
 * the full SYSCLK. Prescale to 1 MHz and extend to 64 bits in software. */
static void timebase_init(void) {
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    (void)RCC->APB1LENR;
    TIM2->CR1 = 0;
    TIM2->PSC = (uint16_t)(SystemCoreClock / 1000000u - 1u);
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = TIM_EGR_UG; /* latch PSC */
    TIM2->CR1 = TIM_CR1_CEN;

    /* DWT cycle counter, used only to cross-check the achieved CPU clock. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint64_t tamp_bench_time_us(void) {
    static uint32_t last;
    static uint64_t high;
    uint32_t now = TIM2->CNT;
    if (now < last) high += 0x100000000ull;
    last = now;
    return high | now;
}

void tamp_system_init(void) {
    clock_280mhz_ok = clock_init_280mhz();
    SCB_EnableICache();
    SCB_EnableDCache();
    timebase_init();
}

void tamp_system_report(void) {
    /* Measure the CPU clock against the 1 MHz timebase so a mis-set PSC or
     * PLL shows up as a mismatch instead of silently scaling every result. */
    uint32_t t0 = TIM2->CNT;
    uint32_t c0 = DWT->CYCCNT;
    while ((uint32_t)(TIM2->CNT - t0) < 50000u)
        ; /* 50 ms */
    uint32_t cycles = DWT->CYCCNT - c0;
    uint32_t us = TIM2->CNT - t0;

    printf("INFO stm32h7b0 sysclk_hz=%lu clock_280mhz_ok=%d measured_cpu_mhz=%lu\n", (unsigned long)SystemCoreClock,
           clock_280mhz_ok ? 1 : 0, (unsigned long)(cycles / us));
    printf("INFO icache=%d dcache=%d flash_latency=%lu vos=%lu\n", (SCB->CCR & SCB_CCR_IC_Msk) ? 1 : 0,
           (SCB->CCR & SCB_CCR_DC_Msk) ? 1 : 0, (unsigned long)(FLASH->ACR & FLASH_ACR_LATENCY),
           (unsigned long)((PWR->SRDCR & PWR_SRDCR_VOS) >> PWR_SRDCR_VOS_Pos));
}
