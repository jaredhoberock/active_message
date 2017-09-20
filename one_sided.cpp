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

#include "active_message.hpp"


void active_message_handler(void* data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
{
  // get the message
  const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
  active_message message = from_string<active_message>(data_buffer, buffer_size);

  // activate the message
  message.activate();
}

void hello_world(int value)
{
  std::cout << "Hello, world from PE " << shmem_my_pe() << " with value " << value << "!" << std::endl;
}

int main()
{
  shmem_init();

  shmemx_am_attach(0, active_message_handler);

  if(shmem_my_pe() == 0)
  {
    active_message message(hello_world, 7);

    // serialize and transmit message
    std::string serialized = to_string(message);
    shmemx_am_request(1, 0, const_cast<char*>(serialized.data()), serialized.size());
  }
  else
  {
    active_message message(hello_world, 13);

    // serialize and transmit message
    std::string serialized = to_string(message);
    shmemx_am_request(0, 0, const_cast<char*>(serialized.data()), serialized.size());
  }

  shmemx_am_quiet();

  shmem_barrier_all();
}

