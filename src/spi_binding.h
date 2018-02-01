/*
    Copyright (c) 2012, Russell Hay <me@russellhay.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#pragma once

#include <v8.h>
#include <node.h>
#include <node_object_wrap.h>

using namespace v8;
using namespace node;

#define SPI_FUNC(NAME) static void NAME (const FunctionCallbackInfo<Value>& args)
#define SPI_FUNC_IMPL(NAME) void Spi::NAME (const FunctionCallbackInfo<Value>& args)
#define SPI_FUNC_EMPTY(NAME) void Spi::NAME (const FunctionCallbackInfo<Value>& args) { \
    args.GetReturnValue().Set(false);  \
}

#define BCM2708_PERI_BASE        0x3F000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)
 
// I/O access
volatile unsigned *gpio;
void *gpio_map;

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))
 
#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0
 
#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH
 
#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock


class Spi : public ObjectWrap {
    public:
        static Persistent<Function> constructor;
        static void Initialize(Handle<Object> target);

    private:
        Spi() : m_fd(-1),
	        m_mode(0),
	        m_max_speed(1000000),  // default speed in Hz () 1MHz
	        m_delay(0),            // expose delay to options
	        m_bits_per_word(8),    // default bits per word
                m_wr_pin(0),
                m_rdy_pin(0),
                m_invert_rdy(false) {}   // RDY is RDY, not BUSY



          ~Spi() { } // Probably close fd if it's open

        SPI_FUNC(New);
        SPI_FUNC(Open);
        SPI_FUNC(Close);
        SPI_FUNC(Transfer);
        SPI_FUNC(GetSetMode);
        SPI_FUNC(GetSetChipSelect);
        SPI_FUNC(GetSetMaxSpeed);
        SPI_FUNC(GetSet3Wire);
        SPI_FUNC(GetSetDelay);
        SPI_FUNC(GetSetLoop);
        SPI_FUNC(GetSetBitOrder);
        SPI_FUNC(GetSetBitsPerWord);
        SPI_FUNC(GetSetWrPin);
        SPI_FUNC(GetSetRdyPin);
        SPI_FUNC(GetSetInvertRdy);
        SPI_FUNC(GetSetbSeries);

        void full_duplex_transfer(Isolate* isolate, const FunctionCallbackInfo<Value> &args, char *write, char *read, size_t length, uint32_t speed, uint16_t delay, uint8_t bits);
        bool require_arguments(Isolate* isolate, const FunctionCallbackInfo<Value>& args, int count);
        bool get_argument(Isolate *isolate, const FunctionCallbackInfo<Value>& args, int offset, int& value);
        bool get_argument(Isolate *isolate, const FunctionCallbackInfo<Value>& args, int offset, bool& value);
        bool get_argument_greater_than(Isolate *isolate, const FunctionCallbackInfo<Value>& args, int offset, int target, int& value);
        bool get_if_no_args(Isolate *isolate, const FunctionCallbackInfo<Value>& args, int offset, unsigned int value);
        bool get_if_no_args(Isolate *isolate, const FunctionCallbackInfo<Value>& args, int offset, bool value);

        void get_set_mode_toggle(Isolate *isolate, const FunctionCallbackInfo<Value>& args, int mask);

        int m_fd;
        uint32_t m_mode;
        uint32_t m_max_speed;
        uint16_t m_delay;
        uint8_t m_bits_per_word;
        uint32_t m_wr_pin;
        uint32_t m_rdy_pin;
        bool m_bseries;
        bool m_invert_rdy;
};

#define EXCEPTION(X) isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, X)))

#define FUNCTION_PREAMBLE                          \
             Isolate* isolate = args.GetIsolate(); \
             HandleScope scope(isolate);           \
             Spi* self = ObjectWrap::Unwrap<Spi>(args.This())

#define FUNCTION_CHAIN args.GetReturnValue().Set(args.This())

#define ASSERT_OPEN if (self->m_fd == -1) { EXCEPTION("Device not opened"); return; } 
#define ASSERT_NOT_OPEN if (self->m_fd != -1) { EXCEPTION("Cannot be called once device is opened"); return; }
#define ONLY_IF_OPEN if (self->m_fd == -1) { FUNCTION_CHAIN; return; }

#define REQ_INT_ARG_GT(I, NAME, VAR, VAL)                                      \
  REQ_INT_ARG(I, VAR);                                                         \
  if (VAR <= VAL)                                                              \
    return ThrowException(Exception::TypeError(                                \
       String::NewFromUtf8(isolate, #NAME " must be greater than " #VAL )));

#define SPI_FUNC_BOOLEAN_TOGGLE_IMPL(NAME, ARGUMENT)                           \
SPI_FUNC_IMPL(NAME) {                                                          \
  FUNCTION_PREAMBLE;                                                           \
  self->get_set_mode_toggle(isolate, args, ARGUMENT);                          \
}

#define SET_IOCTL_VALUE(FD, CTRL, VALUE)                                       \
  retval = ioctl(FD, CTRL, &(VALUE));                                          \
  if (retval == -1) {                                                          \
    EXCEPTION("Unable to set " #CTRL);                                         \
    return;                                                                    \
  }

#define MAX(a,b) (a>b ? a:b)

