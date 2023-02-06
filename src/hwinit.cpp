/*
 * This file is part of the tumanako_vc project.
 *
 * Copyright (C) 2010 Johannes Huebner <contact@johanneshuebner.com>
 * Copyright (C) 2010 Edward Cheeseman <cheesemanedward@gmail.com>
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/stm32/can.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/desig.h>
#include <libopencm3/stm32/crc.h>
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/cm3/nvic.h>
#include "hwinit.h"

/**
* Start clocks of all needed peripherals
*/
void clock_setup(void)
{
   rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_HSI_24MHZ]);
   rcc_osc_on(RCC_LSI);

   rcc_periph_clock_enable(RCC_GPIOA);
   rcc_periph_clock_enable(RCC_GPIOB);
   rcc_periph_clock_enable(RCC_GPIOC);
   rcc_periph_clock_enable(RCC_CRC);
   rcc_periph_clock_enable(RCC_CAN1);

   rcc_wait_for_osc_ready(RCC_LSI);
   iwdg_set_period_ms(2000);
   iwdg_start();
}

void can_setup(int masterId)
{
   gpio_set_mode(GPIO_BANK_CAN1_RX, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO_CAN1_RX);
   gpio_set(GPIO_BANK_CAN1_RX, GPIO_CAN1_RX);
   // Configure CAN pin: TX
   gpio_set_mode(GPIO_BANK_CAN1_TX, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_CAN1_TX);

   // CAN cell init.
   // Setting the bitrate to 500KBit. APB1 = 24MHz,
   // prescaler = 6 -> 4MHz time quanta frequency.
   // 1tq sync + 6tq bit segment1 (TS1) + 1tq bit segment2 (TS2) =
   // 8time quanto per bit period, therefor 4MHz/16 = 500kHz
   //
   can_init(CAN1,
            false,          // TTCM: Time triggered comm mode?
            true,           // ABOM: Automatic bus-off management?
            false,          // AWUM: Automatic wakeup mode?
            false,          // NART: No automatic retransmission?
            false,          // RFLM: Receive FIFO locked mode?
            false,          // TXFP: Transmit FIFO priority?
            CAN_BTR_SJW_1TQ,
            CAN_BTR_TS1_6TQ,
            CAN_BTR_TS2_1TQ,
            6,				   // BRP: Baud rate prescaler
            false,
            false);

   //register master ID
   can_filter_id_list_16bit_init(0, masterId << 5, 0, 0, 0, 0, true);
   nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);
   can_enable_irq(CAN1, CAN_IER_FMPIE0);
}

void can_teardown()
{
   can_reset(CAN1);
   nvic_disable_irq(NVIC_USB_LP_CAN_RX0_IRQ);
}

//Left over for future usage in an inverter style device..
// Todo: read an application configuration based on fingerprint?
//
void initialize_pins()
{
   uint32_t flashSize = desig_get_flash_size();
   uint32_t pindefAddr = FLASH_BASE + flashSize * 1024 - PINDEF_BLKNUM * PINDEF_BLKSIZE;
   const struct pincommands* pincommands = (struct pincommands*)pindefAddr;

   uint32_t crc = crc_calculate_block(((uint32_t*)pincommands), PINDEF_NUMWORDS);

   if (crc == pincommands->crc)
   {
      for (int idx = 0; idx < NUM_PIN_COMMANDS && pincommands->pindef[idx].port > 0; idx++)
      {
         uint8_t cnf = pincommands->pindef[idx].inout ? GPIO_CNF_OUTPUT_PUSHPULL : GPIO_CNF_INPUT_PULL_UPDOWN;
         uint8_t mode = pincommands->pindef[idx].inout ? GPIO_MODE_OUTPUT_50_MHZ : GPIO_MODE_INPUT;
         gpio_set_mode(pincommands->pindef[idx].port, mode, cnf, pincommands->pindef[idx].pin);

         if (pincommands->pindef[idx].level)
         {
            gpio_set(pincommands->pindef[idx].port, pincommands->pindef[idx].pin);
         }
      }
   }
}
