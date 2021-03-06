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

// #define BUILDING_NODE_EXTENSION

#include "spi_binding.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>


#ifdef __linux__
  #include <sys/ioctl.h>
  #include <linux/spi/spidev.h>
#else
  #include "fake_spi.h"
#endif
#include <node.h>
#include <node_buffer.h>

using namespace v8;
using namespace node;

extern "C" {
  void init (Handle<Object> target) {
    Spi::Initialize(target);
  }

  void delayMicrosecondsHard (unsigned int howLong)
 {
   struct timeval tNow, tLong, tEnd ;
 
   gettimeofday (&tNow, NULL) ;
   tLong.tv_sec  = howLong / 1000000 ;
   tLong.tv_usec = howLong % 1000000 ;
   timeradd (&tNow, &tLong, &tEnd) ;
 
   while (timercmp (&tNow, &tEnd, <))
     gettimeofday (&tNow, NULL) ;
 }

  NODE_MODULE(_spi, init)
}

Persistent<Function> Spi::constructor;

void Spi::Initialize(Handle<Object> target) {
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  // var t = function() {};
  Local<FunctionTemplate> t = FunctionTemplate::New(isolate, New);

  // t = function _spi() {};
  t->SetClassName(String::NewFromUtf8(isolate, "_spi"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  NODE_SET_PROTOTYPE_METHOD(t, "open", Open);
  NODE_SET_PROTOTYPE_METHOD(t, "close", Close);
  NODE_SET_PROTOTYPE_METHOD(t, "transfer", Transfer);
  NODE_SET_PROTOTYPE_METHOD(t, "mode", GetSetMode);
  NODE_SET_PROTOTYPE_METHOD(t, "chipSelect", GetSetChipSelect);
  NODE_SET_PROTOTYPE_METHOD(t, "size", GetSetBitsPerWord);
  NODE_SET_PROTOTYPE_METHOD(t, "bitOrder", GetSetBitOrder);
  NODE_SET_PROTOTYPE_METHOD(t, "maxSpeed", GetSetMaxSpeed);
  NODE_SET_PROTOTYPE_METHOD(t, "halfDuplex", GetSet3Wire);
  NODE_SET_PROTOTYPE_METHOD(t, "delay", GetSetDelay);
  NODE_SET_PROTOTYPE_METHOD(t, "loopback", GetSetLoop);
  NODE_SET_PROTOTYPE_METHOD(t, "wrPin", GetSetWrPin);
  NODE_SET_PROTOTYPE_METHOD(t, "rdyPin", GetSetRdyPin);
  NODE_SET_PROTOTYPE_METHOD(t, "invertRdy", GetSetInvertRdy);
  NODE_SET_PROTOTYPE_METHOD(t, "bSeries", GetSetbSeries);

  // var constructor = t; // in context of new.
  constructor.Reset(isolate, t->GetFunction());

  // exports._spi = constructor;
  target->Set(String::NewFromUtf8(isolate, "_spi"), t->GetFunction());

  NODE_DEFINE_CONSTANT(target, SPI_MODE_0);
  NODE_DEFINE_CONSTANT(target, SPI_MODE_1);
  NODE_DEFINE_CONSTANT(target, SPI_MODE_2);
  NODE_DEFINE_CONSTANT(target, SPI_MODE_3);

#define SPI_CS_LOW 0  // This doesn't exist normally
  NODE_DEFINE_CONSTANT(target, SPI_NO_CS);
  NODE_DEFINE_CONSTANT(target, SPI_CS_HIGH);
  NODE_DEFINE_CONSTANT(target, SPI_CS_LOW);

#define SPI_MSB false
#define SPI_LSB true
  NODE_DEFINE_CONSTANT(target, SPI_MSB);
  NODE_DEFINE_CONSTANT(target, SPI_LSB);

}

// new Spi(string device)
//Handle<Value> Spi::New(const FunctionCallbackInfo<Value>& args) {
SPI_FUNC_IMPL(New) {
  Isolate *isolate = args.GetIsolate();
  HandleScope scope(isolate);

  if (args.IsConstructCall()) {
    Spi* spi = new Spi();
    spi->Wrap(args.This());
    args.GetReturnValue().Set(args.This());
  } else {
    Local<Function> cons = Local<Function>::New(isolate, constructor);
    args.GetReturnValue().Set(cons->NewInstance());
  }
}

// TODO: Make Non-blocking once basic functionality is proven
void Spi::Open(const FunctionCallbackInfo<Value>& args) {
  FUNCTION_PREAMBLE;
  if (!self->require_arguments(isolate, args, 1)) { return; }
  ASSERT_NOT_OPEN;

  String::Utf8Value device(args[0]->ToString());
  int retval = 0;

  self->m_fd = open(*device, O_RDWR); // Blocking!
  if (self->m_fd < 0) {
    EXCEPTION("Unable to open device");
    return;
  }

  SET_IOCTL_VALUE(self->m_fd, SPI_IOC_WR_MODE, self->m_mode);
  SET_IOCTL_VALUE(self->m_fd, SPI_IOC_WR_BITS_PER_WORD, self->m_bits_per_word);
  SET_IOCTL_VALUE(self->m_fd, SPI_IOC_WR_MAX_SPEED_HZ, self->m_max_speed);

  // Setup the GPIO pin as well
  int mem_fd;
  /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      EXCEPTION("can't open /dev/mem");
      return;
   }
 
   /* mmap GPIO */
   gpio_map = mmap(
      NULL,             //Any adddress in our space will do
      BLOCK_SIZE,       //Map length
      PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
      MAP_SHARED,       //Shared with other processes
      mem_fd,           //File to map
      GPIO_BASE         //Offset to GPIO peripheral
   );
 
   close(mem_fd); //No need to keep mem_fd open after mmap
 
   if (gpio_map == MAP_FAILED) {
      EXCEPTION("mmap error");//errno also set!
      
   }

   printf("Ready pin: %u", self->m_rdy_pin);
 
   // Always use volatile pointer!
   gpio = (volatile unsigned *)gpio_map;

   INP_GPIO(self->m_wr_pin);
   OUT_GPIO(self->m_wr_pin);

   INP_GPIO(self->m_rdy_pin);
   // Enable pulldown on Ready pin:
   GPIO_PULL = 1;
   delayMicrosecondsHard(5);
   GPIO_PULLCLK0 = 1 << self->m_rdy_pin;
   delayMicrosecondsHard(5);
   GPIO_PULL = 0;
   GPIO_PULLCLK0 = 0;   

  FUNCTION_CHAIN;
}

void Spi::Close(const FunctionCallbackInfo<Value> &args) {
  FUNCTION_PREAMBLE;
  ONLY_IF_OPEN;

  close(self->m_fd);
  self->m_fd = -1;

  FUNCTION_CHAIN;
}

// tranfer(write_buffer, read_buffer);
void Spi::Transfer(const FunctionCallbackInfo<Value> &args) {
  FUNCTION_PREAMBLE;
  ASSERT_OPEN;
    if (!self->require_arguments(isolate, args, 2)) { return; }

  if ((args[0]->IsNull()) && (args[1]->IsNull())) {
    EXCEPTION("Both buffers cannot be null");
    return;
  }

  char *write_buffer = NULL;
  char *read_buffer = NULL;
  size_t write_length = -1;
  size_t read_length = -1;
  Local<Object> write_buffer_obj;
  Local<Object> read_buffer_obj;

  if (Buffer::HasInstance(args[0])) {
    write_buffer_obj = args[0]->ToObject();
    write_buffer = Buffer::Data(write_buffer_obj);
    write_length = Buffer::Length(write_buffer_obj);
  }

  if (Buffer::HasInstance(args[1])) {
    read_buffer_obj = args[1]->ToObject();
    read_buffer = Buffer::Data(read_buffer_obj);
    read_length = Buffer::Length(read_buffer_obj);
  }

  if (write_length > 0 && read_length > 0 && write_length != read_length) {
    EXCEPTION("Read and write buffers MUST be the same length");
    return;
  }

  self->full_duplex_transfer(isolate, args, write_buffer, read_buffer,
                                MAX(write_length, read_length),
                                self->m_max_speed, self->m_delay, self->m_bits_per_word);

}

void Spi::full_duplex_transfer(
  Isolate *isolate,
  const FunctionCallbackInfo<Value> &args,
  char *write,
  char *read,
  size_t length,
  uint32_t speed,
  uint16_t delay,
  uint8_t bits
) {
  Spi* self = ObjectWrap::Unwrap<Spi>(args.This());
  struct spi_ioc_transfer data = {
	  (unsigned long)write,
	  (unsigned long)read,
	  1,
	  speed,
	  delay,
	  bits,
    0,   // cs_change
    0,  // tx_nbits
    0,  // rx_nbits
    0   // pad
  };

  int ret =0;

  GPIO_SET = 1 << self->m_wr_pin;

  if (self->m_invert_rdy) {
    while(GET_GPIO(self->m_rdy_pin)){};
  } else {
    while(!GET_GPIO(self->m_rdy_pin)){};
  }  

  // Now send byte by byte for the whole buffer
  while (length--) {
    ret = ioctl(this->m_fd, SPI_IOC_MESSAGE(1), &data);

    if (self->m_wr_pin) {
      GPIO_CLR = 1 << self->m_wr_pin;
      GPIO_SET = 1 << self->m_wr_pin;
    }

    if (self->m_invert_rdy) {
      //For Series 7000 displays, the busy pin (spec says 20us max!)
      // can take a while to go up, so we have to add this delay. 10us
      // works well in practice.
      delayMicrosecondsHard(10); 
      while(GET_GPIO(self->m_rdy_pin)){};
    } else {
      // The RDY line can take up to 500ns to do down,
      // so we need to wait before reading it:
      delayMicrosecondsHard(1); 
      while(!GET_GPIO(self->m_rdy_pin)){};
    }
    //delayMicrosecondsHard(15);
    
    data.tx_buf++;
   }

  if (ret == -1) {
    EXCEPTION("Unable to send SPI message");
    return;
  }

  args.GetReturnValue().Set(ret);
}

// This overrides any of the OTHER set functions since modes are predefined
// sets of options.
SPI_FUNC_IMPL(GetSetMode) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, self->m_mode)) { return; }
  int in_mode;
  if (!self->get_argument(isolate, args, 0, in_mode)) { return; }

  ASSERT_NOT_OPEN;

  if (in_mode == SPI_MODE_0 || in_mode == SPI_MODE_1 ||
      in_mode == SPI_MODE_2 || in_mode == SPI_MODE_3) {
    self->m_mode = in_mode;
  } else {
    EXCEPTION("Argument 1 must be one of the SPI_MODE_X constants");
    return;
  }

  FUNCTION_CHAIN;
}
SPI_FUNC_IMPL(GetSetChipSelect) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, self->m_mode&(SPI_CS_HIGH|SPI_NO_CS))) { return; }
  int in_value;
  if (!self->get_argument(isolate, args, 0, in_value)) { return; }

  ASSERT_NOT_OPEN;

  switch(in_value) {
    case SPI_CS_HIGH:
      self->m_mode |= SPI_CS_HIGH;
      self->m_mode &= ~SPI_NO_CS;
      break;
    case SPI_NO_CS:
      self->m_mode |= SPI_NO_CS;
      self->m_mode &= ~SPI_CS_HIGH;
      break;
    default:
      self->m_mode &= ~(SPI_NO_CS|SPI_CS_HIGH);
      break;
  }

  FUNCTION_CHAIN;
}

SPI_FUNC_IMPL(GetSetBitsPerWord) {
  FUNCTION_PREAMBLE;
  if (self->get_if_no_args(isolate, args, 0, (unsigned int)self->m_bits_per_word)) { return; }

  int in_value;
  if (!self->get_argument_greater_than(isolate, args, 0, 0, in_value)) { return; }
  ASSERT_NOT_OPEN;

  // TODO: Bounds checking?  Need to look up what the max value is
  self->m_bits_per_word = in_value;

  FUNCTION_CHAIN;
}

SPI_FUNC_IMPL(GetSetMaxSpeed) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, self->m_max_speed)) { return; }

  int in_value;
  if (!self->get_argument_greater_than(isolate, args, 0, 0, in_value)) { return; }
  ASSERT_NOT_OPEN;

  // TODO: Bounds Checking? Need to look up what the max value is
  self->m_max_speed = in_value;

  FUNCTION_CHAIN;
}

SPI_FUNC_IMPL(GetSetWrPin) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, (unsigned int)self->m_wr_pin)) { return; }

  int in_value;
  if (!self->get_argument_greater_than(isolate, args, 0, 0, in_value)) { return; }
  ASSERT_NOT_OPEN;

  // TODO: Bounds Checking? Need to look up what the max value is
  self->m_wr_pin = in_value;

  FUNCTION_CHAIN;
}

SPI_FUNC_IMPL(GetSetRdyPin) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, (unsigned int)self->m_rdy_pin)) { return; }

  int in_value;
  if (!self->get_argument_greater_than(isolate, args, 0, 0, in_value)) { return; }
  ASSERT_NOT_OPEN;

  // TODO: Bounds Checking? Need to look up what the max value is
  self->m_rdy_pin = in_value;

  FUNCTION_CHAIN;
}

SPI_FUNC_IMPL(GetSetInvertRdy) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, self->m_invert_rdy)) { return; }

  bool in_value;
  if (!self->get_argument(isolate, args, 0, in_value)) { return; }

  self->m_invert_rdy = in_value;

  FUNCTION_CHAIN;
}


SPI_FUNC_IMPL(GetSetbSeries) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, self->m_bseries)) { return; }

  bool in_value;
  if (!self->get_argument(isolate, args, 0, in_value)) { return; }

  self->m_bseries = in_value;

  FUNCTION_CHAIN;
}



SPI_FUNC_IMPL(GetSet3Wire) {
  FUNCTION_PREAMBLE;

  if (self->get_if_no_args(isolate, args, 0, (self->m_mode&SPI_3WIRE) > 0)) { return; }

  bool in_value;
  if (!self->get_argument(isolate, args, 0, in_value)) { return; }

  if (in_value) {
    self->m_mode |= SPI_3WIRE;
  } else {
    self->m_mode &= ~SPI_3WIRE;
  }
  FUNCTION_CHAIN;
}

// Expose m_delay as "delay"
SPI_FUNC_IMPL(GetSetDelay) {
	FUNCTION_PREAMBLE;

	if (self->get_if_no_args(isolate, args, 0, (unsigned int)self->m_delay)) { return; }

  int in_value;
	if (!self->get_argument_greater_than(isolate, args, 0, 0, in_value)) { return; }
	ASSERT_NOT_OPEN;

	self->m_delay = in_value;

	FUNCTION_CHAIN;
}

SPI_FUNC_BOOLEAN_TOGGLE_IMPL(GetSetLoop, SPI_LOOP);
SPI_FUNC_BOOLEAN_TOGGLE_IMPL(GetSetBitOrder, SPI_LSB_FIRST);

#define ERROR_EXPECTED_ARGUMENTS(N) "Expected " #N "arguments"
#define ERROR_ARGUMENT_NOT_INTEGER(I) "Argument " #I " must be an integer"
#define ERROR_ARGUMENT_NOT_BOOLEAN(I) "Argument " #I " must be an boolean"
#define ERROR_OUT_OF_RANGE(I, V, R, M) "Argument " #I " must be " #R " " #M " but was " #V

/********************************************************************************************************************* 

Internal Functions */

bool
Spi::require_arguments(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int count
) {
    if (args.Length() < count) {
      EXCEPTION(ERROR_EXPECTED_ARGUMENTS(count));
      return false;
    }

    return true;
}

bool
Spi::get_argument(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int offset,
  int& value
) {
  if (args.Length() <= offset || !args[offset]->IsInt32()) {
    EXCEPTION(ERROR_ARGUMENT_NOT_INTEGER(offset));
    return false;
  }

  value = args[offset]->Int32Value();
  return true;
}

bool
Spi::get_argument(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int offset,
  bool& value
) {
  if (args.Length() <= offset || !args[offset]->IsBoolean()) {
    EXCEPTION(ERROR_ARGUMENT_NOT_BOOLEAN(offset));
    return false;
  }

  value = args[offset]->BooleanValue();
  return true;
}

bool
Spi::get_if_no_args(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int offset,
  unsigned int value
) {
  if (args.Length() <= offset) {
    args.GetReturnValue().Set(value);
    return true;
  }

  return false;
}

bool
Spi::get_if_no_args(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int offset,
  bool value
) {
  if (args.Length() <= offset) {
    args.GetReturnValue().Set(value);
    return true;
  }

  return false;
}

bool
Spi::get_argument_greater_than(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int offset,
  int target,
  int& value
) {
  if (!get_argument(isolate, args, offset, value)) { return false; }

  if (value <= target) {
    EXCEPTION(ERROR_OUT_OF_RANGE(offset, value, >, target));
    return false;
  }

  return true;
}

void
Spi::get_set_mode_toggle(
  Isolate *isolate,
  const FunctionCallbackInfo<Value>& args,
  int mask
) {
    if (get_if_no_args(isolate, args, 0, (m_mode&mask) > 0)) { return; }

    bool in_value;
    if (!get_argument(isolate, args, 0, in_value)) { return; }

    if (in_value) {                                                              
      m_mode |= mask;                                                  
    } else {                                                                     
      m_mode &= ~mask;                                                 
    }                                                                            
    FUNCTION_CHAIN;                                                              
}

