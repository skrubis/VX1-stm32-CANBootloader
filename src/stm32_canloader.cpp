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
#include <stdint.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/can.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/stm32/desig.h>
#include <libopencm3/stm32/crc.h>
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/scb.h>
#include "hwinit.h"

#define FLASH_START         0x08000000
#define SMALLEST_PAGE_WORDS 256
#define PROGRAM_WORDS       512
#define APP_FLASH_START     0x08001000
#define BOOTLOADER_MAGIC    0xAA
#define DELAY_100           (1 << 17)
#define nodeCANID           0x7DE
#define masterCANID         0x7DD

enum states
{
   MAGIC, PAGECOUNT, PAGE, CRC, PROGRAM, DONE
};

static states state = MAGIC;
static uint32_t page_buffer[PROGRAM_WORDS];

//Check 1k of flash whether it contains only 0xFF = erased
static bool check_erased(uint32_t* baseAddress)
{
   uint32_t check = 0xFFFFFFFF;

   for (int i = 0; i < SMALLEST_PAGE_WORDS; i++, baseAddress++)
      check &= *baseAddress;

   return check == 0xFFFFFFFF;
}

//We always write 2kb pages. After erasing the possible first page we check the
//data content of the possible second page. If it is not erased, it will be.
static void write_flash(uint32_t addr, uint32_t *pageBuffer)
{
   flash_erase_page(addr);

   if (!check_erased(((uint32_t*)addr) + SMALLEST_PAGE_WORDS))
      flash_erase_page(addr + SMALLEST_PAGE_WORDS * 4);

   for (uint32_t idx = 0; idx < PROGRAM_WORDS; idx++)
   {
      flash_program_word(addr + idx * 4, pageBuffer[idx]);
   }
}

static void can_send_byte(uint8_t b)
{
   can_transmit(CAN1, nodeCANID, false, false, 1, &b);
}

static bool can_recv(uint8_t* data)
{
   uint32_t id;
   bool ext, rtr;
   uint8_t fmi, len;

   return can_receive(CAN1, 0, true, &id, &ext, &rtr, &fmi, &len, data, 0) > 0;
}

static void wait()
{
   for (volatile uint32_t i = DELAY_100; i > 0; i--);
}


extern "C" int main(void)
{
   uint32_t addr = APP_FLASH_START;

   clock_setup();
   initialize_pins();
   can_setup(masterCANID);

   can_send_byte('2');

   wait();

   if (state == PAGECOUNT || state == PAGE)
   {
      flash_unlock();

      while (state != DONE)
      {
         if (state == PROGRAM)
         {
            write_flash(addr, page_buffer);
            addr += sizeof(page_buffer);
            state = PAGE;
            can_send_byte('P');
         }
         iwdg_reset();
      }

      flash_lock();
   }

   //We are done lets tell the world this!!
   can_send_byte('D');

   wait();

   can_teardown();

   void (*app_main)(void) = (void (*)(void)) *(volatile uint32_t*)(APP_FLASH_START + 4);
   SCB_VTOR = APP_FLASH_START;
   app_main();

   return 0;
}

/* Interrupt service routines */
extern "C" void usb_lp_can_rx0_isr(void)
{
   uint8_t canData[8];
   uint32_t* words = (uint32_t*)canData;
   static uint8_t numPages = 0;
   static uint32_t currentWord = 0;
   static uint32_t crc;

   can_recv(canData);

   switch (state)
   {
   case MAGIC:
      if (canData[0] == BOOTLOADER_MAGIC)
      {
         can_send_byte('S');
         state = PAGECOUNT;
      }
      break;
   case PAGECOUNT:
      numPages = canData[0];
      state = PAGE;
      currentWord = 0;
      can_send_byte('P');
      crc_reset();
      break;
   case PAGE:
      page_buffer[currentWord++] = words[0];
      page_buffer[currentWord++] = words[1];
      crc_calculate(words[0]);
      crc = crc_calculate(words[1]);

      if (currentWord == PROGRAM_WORDS)
      {
         state = CRC;
         can_send_byte('C');
      }
      else
      {
         can_send_byte('P');
      }
      break;
   case CRC:
      currentWord = 0;
      crc_reset();
      if (words[0] == crc)
      {
         numPages--;
         if (numPages == 0)
         {
            state = DONE;
         }
         else
         {
            state = PROGRAM;
         }
      }
      else
      {
         can_send_byte('E');
         state = PAGE;
      }
      break;
   case PROGRAM:
      //Flash programming done in main()
      break;
   case DONE:
      //Nothing to do!
      break;
   }
}
