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

// $ ./openshmem-am-root/bin/oshc++ -std=c++14 future.cpp 
// $ ./openshmem-am-root/bin/oshrun ./a.out -n 2
// PE 1: Hello, world with value 7!
// PE 0: received 13


#include <shmemx.h>
#include <iostream>
#include <utility>
#include <string>
#include <future>

#include "active_message.hpp"


void active_message_handler(void* data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
{
  // get the message
  const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
  two_sided_active_message message = from_string<two_sided_active_message>(data_buffer, buffer_size);

  // activate the message and get the reply
  active_message reply = message.activate();

  // transmit reply
  std::string serialized = to_string(reply);
  shmemx_am_reply(1, const_cast<char*>(serialized.data()), serialized.size(), token);
}


void active_message_reply_handler(void *data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
{
  // get the reply
  const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
  active_message reply = from_string<active_message>(data_buffer, buffer_size);

  // activate it
  reply.activate();
}


int hello_world(int value)
{
  std::cout << "PE " << shmem_my_pe() << ": Hello, world with value " << value << "!" << std::endl;

  return 13;
}


int fulfill_and_delete_promise(int result, std::promise<int>* p)
{
  std::cout << "PE " << shmem_my_pe() << ": received " << result << std::endl;

  p->set_value(result);

  delete p;

  return 0;
}


int main()
{
  shmem_init();

  shmemx_am_attach(0, active_message_handler);
  shmemx_am_attach(1, active_message_reply_handler);

  if(shmem_my_pe() == 0)
  {
    std::promise<int>* promise = new std::promise<int>();

    std::future<int> future = promise->get_future();

    two_sided_active_message message(hello_world, std::make_tuple(7),
                                     fulfill_and_delete_promise, std::make_tuple(promise));

    // serialize and transmit message
    std::string serialized = to_string(message);
    shmemx_am_request(1, 0, const_cast<char*>(serialized.data()), serialized.size());

    std::cout << "PE 0: Waiting on future" << std::endl;
    future.wait();

    std::cout << "PE 0: Future satisfied" << std::endl;
  }
}

