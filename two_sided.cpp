// Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// $ ./openshmem-am-root/bin/oshc++ -std=c++14 active_message.cpp 
// $ ./openshmem-am-root/bin/oshrun ./a.out -n 2
// Hello, world from PE 1 with value 7!
// Hello, world from PE 0 with value 13!

#include <shmemx.h>
#include <iostream>
#include <cassert>
#include <thread>
#include <cstring>
#include <vector>
#include <sstream>
#include <utility>
#include <type_traits>

#include "serialization.hpp"
#include "string_view_stream.hpp"


class active_message
{
  public:
    active_message() = default;

    template<class FunctionPtr, class... Args>
    explicit active_message(FunctionPtr func, Args... args)
      : message_(func, args...)
    {}

    any activate() const
    {
      return message_();
    }

    template<class OutputArchive>
    friend void serialize(OutputArchive& ar, const active_message& self)
    {
      ar(self.message_);
    }

    template<class InputArchive>
    friend void deserialize(InputArchive& ar, active_message& self)
    {
      ar(self.message_);
    }

  private:
    serializable_closure message_;
};


class two_sided_active_message : private active_message
{
  private:
    using super_t = active_message;

    // two_sided_active_message's constructor initializes the base active_message class with this function
    // as the function to call. The user's functions and arguments passed to two_sided_active_message's constructor are this function's arguments.
    // the result of this function is an active_message containing the reply
    template<class FunctionPtr1, class FunctionPtr2, class... Args>
    static active_message invoke_and_return_active_message_reply(FunctionPtr1 reply_func, FunctionPtr2 func, Args... args)
    {
      // invoke the user's function
      auto user_result = func(args...);

      // make an active_message containing the reply
      return active_message(reply_func, user_result);
    }

  public:
    two_sided_active_message() = default;

    template<class FunctionPtr1, class FunctionPtr2, class... Args>
    two_sided_active_message(FunctionPtr1 reply_func, FunctionPtr2 func, Args... args)
      : super_t(&invoke_and_return_active_message_reply<FunctionPtr1,FunctionPtr2,Args...>, reply_func, func, args...)
    {}

    active_message activate() const
    {
      return any_cast<active_message>(super_t::activate());
    }

    template<class OutputArchive>
    friend void serialize(OutputArchive& ar, const two_sided_active_message& self)
    {
      ar(static_cast<const active_message&>(self));
    }

    template<class InputArchive>
    friend void deserialize(InputArchive& ar, two_sided_active_message& self)
    {
      ar(static_cast<active_message&>(self));
    }
};


void active_message_handler(void* data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
{
  // deserialize the message
  two_sided_active_message message;
  {
    const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
    string_view_stream is(data_buffer, buffer_size);
    input_archive ar(is);
    ar(message);
  }

  active_message reply = message.activate();

  {
    // serialize and transmit reply
    std::stringstream os;
    output_archive ar(os);
    ar(reply);
    std::string serialized = os.str();
    shmemx_am_reply(1, const_cast<char*>(serialized.data()), serialized.size(), token);
  }
}


void active_message_reply_handler(void *data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
{
  // deserialize the reply
  active_message reply;
  {
    const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
    string_view_stream is(data_buffer, buffer_size);
    input_archive ar(is);
    ar(reply);
  }

  reply.activate();
}


int hello_world(int value)
{
  std::cout << "PE " << shmem_my_pe() << ": Hello, world with value " << value << "!" << std::endl;

  return 13;
}


int reply(int value)
{
  std::cout << "PE " << shmem_my_pe() << ": received " << value << std::endl;
  return 0;
}


int main()
{
  shmem_init();

  shmemx_am_attach(0, active_message_handler);
  shmemx_am_attach(1, active_message_reply_handler);

  if(shmem_my_pe() == 0)
  {
    two_sided_active_message message(reply, hello_world, 7);

    // serialize and transmit message
    std::stringstream os;
    output_archive ar(os);
    ar(message);
    std::string serialized = os.str();
    shmemx_am_request(1, 0, const_cast<char*>(serialized.data()), serialized.size());
  }

  shmemx_am_quiet();

  shmem_barrier_all();
}

